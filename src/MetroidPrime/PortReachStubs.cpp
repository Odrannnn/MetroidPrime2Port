/**
 * Port REACHABILITY STUBS - GENERATED, DIAGNOSTIC ONLY, do not hand-edit.
 *
 *   generator: tools/gen_link_stubs.py --reachable
 *   input:     docs/research/boot_path_reachable.tsv  (from tools/link_reach.py)
 *   built by:  -DMP_BOOT_STUBS=ON, which NOTHING but tools/boot_probe.sh passes
 *
 * **THIS IS NOT PART OF THE PORT.** Every symbol here is referenced by an object the boot path
 * *does* reach, so these definitions are lies: the program will run, and then behave wrongly.
 * `link_check.sh` never sees this file, and the real link still fails on all
 * 317 of them.
 *
 * **What it is for.** `link_reach.py` can say which symbols are reachable. It cannot say which
 * one the game asks for *first*, or in what order - and the order is what tells a lane what to
 * write next. So each stub logs its own name on entry and returns. One run prints the sequence
 * the boot path actually demands:
 *
 *   [0007] _ZN13CGameAllocator10InitializeER10COsContext
 *   [0008] _ZN9CGameStateC1ER11CInputStreami
 *
 * A non-void function stubbed as `void()` returns whatever was in the return register, so the
 * port may fault later than the first stub. That is expected; the log up to the fault is still
 * ordered evidence, and the fault is itself the next thing to find.
 *
 * **`AllocateRenderer` was removed from here by hand on 2026-09-26** (lane `pixels`), because
 * `src/MetaRender/Carve8026EF54.cpp` now defines that symbol for real. It was the only alias
 * deleted this way; re-running the generator over an unfixed `boot_path_reachable.tsv` puts it
 * back, and the two definitions then collide at link time - which the boot probe cannot see,
 * because it links *with* this file. `tools/check_boot_stubs.py` and the gate's `port link dups`
 * step are what catch it. `fn_80049244` never had one.
 *
 * Breakdown, measured with `grep -E '^extern "C" void reachstub_[0-9]+\(\) asm\('` and a split
 * on the name (last recounted 2026-09-28): **293 stubs** - 239 Itanium (`_Z...`), 3
 * `REL_Load*`, 51 unmangled (`fn_`, `lbl_`, `mp_`, `__nw__`). 294 before
 * `_ZN11CSfxManager14TranslateSFXIDEt` was retired below (2026-09-28), 294 before
 * `StreamNewGameState__5CMainFR12CInputStreami`, 298 before
 * `_ZN11CSimplePool11fn_8029c7e8ERK10SObjectTag`, 297 before
 * `_Z27LoadTypedefEditorPropertiesR20SLdrEditorPropertiesR12CInputStream`, 296 before the
 * two `LdrToEntityInfo` aliases were retired below, and 294 before `_ZN10CModelDataD1Ev` was
 * retired and its callee `_ZN9CAnimDataD1Ev` hand-added below (both 2026-09-28).
 * **Superseded 2026-09-29: the counts above are stale.** After the upstream base merge the file
 * no longer linked (duplicates of symbols the merge defined, and new undefined C++ symbols the
 * probe's self-heal cannot add because GNU ld reports them demangled). 109 aliases were retired
 * and 140 entries appended at the end from a `-Wl,--no-demangle` link of the probe. Now: **318
 * stubs** - 258 Itanium, 3 `REL_Load*`, 57 unmangled - plus 6 `reachdata_` data stand-ins.
 *
 * The figure this line carried before that was **317, which was already stale** - the file's
 * own bodies said 296 at the time, so the comment was counting a tree that no longer exists.
 */

#include <cstdio>
#include <cstdlib>

namespace {

unsigned g_stubSeq = 0;

} // namespace

// One definition for all of them: the name is passed, not encoded in a symbol, so this stays
// readable and the cost is one PLT call per stub rather than 318 near-identical bodies.
extern "C" void mpReachStub(const char* mangled, const char* demangled) {
  std::fprintf(stderr, "[reach-stub %04u] %s   (%s)\n", ++g_stubSeq, mangled, demangled);
  std::fflush(stderr);
}

// RETIRED 2026-09-27. src/Kyoto/CARAMManagerPort.cpp defines this for real and is now
// in files.cmake, so this alias is a duplicate under MP_BOOT_STUBS=ON - the
// configuration only tools/boot_probe.sh uses, and the one gate.sh's duplicate
// count cannot see.

// RETIRED 2026-09-27. src/Kyoto/CARAMManagerPort.cpp defines this for real and is now
// in files.cmake, so this alias is a duplicate under MP_BOOT_STUBS=ON - the
// configuration only tools/boot_probe.sh uses, and the one gate.sh's duplicate
// count cannot see.

// RETIRED 2026-09-27. src/Kyoto/CARAMManagerPort.cpp defines this for real and is now
// in files.cmake, so this alias is a duplicate under MP_BOOT_STUBS=ON - the
// configuration only tools/boot_probe.sh uses, and the one gate.sh's duplicate
// count cannot see.

// RETIRED 2026-09-27. src/Kyoto/CARAMManagerPort.cpp defines this for real and is now
// in files.cmake, so this alias is a duplicate under MP_BOOT_STUBS=ON - the
// configuration only tools/boot_probe.sh uses, and the one gate.sh's duplicate
// count cannot see.

// RETIRED 2026-09-27. src/Kyoto/CARAMManagerPort.cpp defines this for real and is now
// in files.cmake, so this alias is a duplicate under MP_BOOT_STUBS=ON - the
// configuration only tools/boot_probe.sh uses, and the one gate.sh's duplicate
// count cannot see.

// RETIRED 2026-09-27. src/Kyoto/CARAMManagerPort.cpp defines this for real and is now
// in files.cmake, so this alias is a duplicate under MP_BOOT_STUBS=ON - the
// configuration only tools/boot_probe.sh uses, and the one gate.sh's duplicate
// count cannot see.

// RETIRED 2026-09-27. src/Kyoto/CARAMManagerPort.cpp defines this for real and is now
// in files.cmake, so this alias is a duplicate under MP_BOOT_STUBS=ON - the
// configuration only tools/boot_probe.sh uses, and the one gate.sh's duplicate
// count cannot see.

// CActor* TCastToPtr<CActor>(CEntity*)
extern "C" void reachstub_8() asm("_Z10TCastToPtrI6CActorEPT_P7CEntity");
extern "C" void reachstub_8() { mpReachStub("_Z10TCastToPtrI6CActorEPT_P7CEntity", "CActor* TCastToPtr<CActor>(CEntity*)"); }

// CActor::AddToRenderer(CFrustumPlanes const&, CStateManager const&) const
extern "C" void reachstub_9() asm("_ZNK6CActor13AddToRendererERK14CFrustumPlanesRK13CStateManager");
extern "C" void reachstub_9() { mpReachStub("_ZNK6CActor13AddToRendererERK14CFrustumPlanesRK13CStateManager", "CActor::AddToRenderer(CFrustumPlanes const&, CStateManager const&) const"); }

// RETIRED 2026-09-26. A reach stub aliased onto a symbol that now has a real definition, so
// listing the decomp unit made the two collide - and the collision is invisible to the gate,
// because this file is added by CMakeLists.txt only under `-DMP_BOOT_STUBS=ON`, which only
// tools/boot_probe.sh passes. `gate.sh`'s `port link dups` step runs without the option, so
// `duplicate definitions 0` is a true statement about a build in which this file is absent.
// tools/boot_probe.sh now counts `multiple definition of` lines and names the symbol. Delete
// the alias here; do NOT remove the decomp unit.

// CActor::SetTransformAlt(CTransform4f const&)
extern "C" void reachstub_11() asm("_ZN6CActor15SetTransformAltERK12CTransform4f");
extern "C" void reachstub_11() { mpReachStub("_ZN6CActor15SetTransformAltERK12CTransform4f", "CActor::SetTransformAlt(CTransform4f const&)"); }

// CActor::UpdateSfxEmitters()
extern "C" void reachstub_13() asm("_ZN6CActor17UpdateSfxEmittersEv");
extern "C" void reachstub_13() { mpReachStub("_ZN6CActor17UpdateSfxEmittersEv", "CActor::UpdateSfxEmitters()"); }

// CActorModelParticles::SetupHook(TUniqueId)
extern "C" void reachstub_19() asm("_ZN20CActorModelParticles9SetupHookE9TUniqueId");
extern "C" void reachstub_19() { mpReachStub("_ZN20CActorModelParticles9SetupHookE9TUniqueId", "CActorModelParticles::SetupHook(TUniqueId)"); }

// CBasics::Stringize(char const*, ...)
extern "C" void reachstub_27() asm("_ZN7CBasics9StringizeEPKcz");
extern "C" void reachstub_27() { mpReachStub("_ZN7CBasics9StringizeEPKcz", "CBasics::Stringize(char const*, ...)"); }

// RETIRED 2026-09-27, stubs 28/29/30. src/MetroidPrime/CCallStack.cpp now defines all three
// for real and is in files.cmake, so these are duplicates under -DMP_BOOT_STUBS=ON - the only
// configuration tools/boot_probe.sh builds, and the one gate.sh's duplicate count cannot see.
// They have now survived three separate collections; the reason is the same each time, so it is
// written here rather than left to be rediscovered.

// CCameraManager::CastGameCameratoFirstPersonCamera(CGameCamera const*)
extern "C" void reachstub_31() asm("_ZN14CCameraManager33CastGameCameratoFirstPersonCameraEPK11CGameCamera");
extern "C" void reachstub_31() { mpReachStub("_ZN14CCameraManager33CastGameCameratoFirstPersonCameraEPK11CGameCamera", "CCameraManager::CastGameCameratoFirstPersonCamera(CGameCamera const*)"); }

// CCameraManager::GetCurrentCamera(CStateManager const&, int) const
extern "C" void reachstub_32() asm("_ZNK14CCameraManager16GetCurrentCameraERK13CStateManageri");
extern "C" void reachstub_32() { mpReachStub("_ZNK14CCameraManager16GetCurrentCameraERK13CStateManageri", "CCameraManager::GetCurrentCamera(CStateManager const&, int) const"); }

// CCharacterFactory::CCharacterFactory(CSimplePool&, TLockedToken<CAnimCharacterSet> const&, unsigned int)
extern "C" void reachstub_34() asm("_ZN17CCharacterFactoryC1ER11CSimplePoolRK12TLockedTokenI17CAnimCharacterSetEj");
extern "C" void reachstub_34() { mpReachStub("_ZN17CCharacterFactoryC1ER11CSimplePoolRK12TLockedTokenI17CAnimCharacterSetEj", "CCharacterFactory::CCharacterFactory(CSimplePool&, TLockedToken<CAnimCharacterSet> const&, unsigned int)"); }

// CCharacterFactory::~CCharacterFactory()
extern "C" void reachstub_35() asm("_ZN17CCharacterFactoryD0Ev");
extern "C" void reachstub_35() { mpReachStub("_ZN17CCharacterFactoryD0Ev", "CCharacterFactory::~CCharacterFactory()"); }

// CEntityInfo::~CEntityInfo()
extern "C" void reachstub_38() asm("_ZN11CEntityInfoD1Ev");
extern "C" void reachstub_38() { mpReachStub("_ZN11CEntityInfoD1Ev", "CEntityInfo::~CEntityInfo()"); }

// RETIRED 2026-09-27. `CEnvFxManager::Initialize` is written and `Matching`
// (0x80166880, 0xEC = 236 bytes) and is in the port build, so this alias is a
// duplicate the moment MP_BOOT_STUBS=ON is on - the configuration only
// tools/boot_probe.sh uses, and the one gate.sh's 'port link dups' step cannot see.
// It was deleted once already and a later collection restored it; the fix belongs here
// permanently, not in whoever happens to collect next.

// CErrorOutputWindow::CErrorOutputWindow(bool)
// RETIRED 2026-09-26. This alias made the linker resolve retail's
// `_ZN18CErrorOutputWindowC1Eb` to a reach stub. `src/MetroidPrime/CErrorOutputWindowCtor.cpp`
// is now listed in `files.cmake` and **defines that name for real**, so the two collided and
// the shipping build failed with `multiple definition of
// 'CErrorOutputWindow::CErrorOutputWindow(bool)'` - in the probe build only.
//
// That it reached the link at all is a **gate gap, not bad luck**: there are 182 `asm("_ZN...")`
// aliases in this file, and `link_check.sh` reported `duplicate definitions 0` on a run it also
// reported as `NOT LINKED`, so the count was vacuous. A duplicate that the gate's source list
// cannot see is exactly the hazard `docs/PROCESS_LESSONS.md` warns about, and it is now checked
// by `tools/check_reach_stub_dups.py` rather than by hoping the link runs.
//
// SUPERSEDED 2026-09-29: `CErrorOutputWindowCtor.cpp` is out of files.cmake again; upstream's
// `CErrorOutputWindow.cpp` (header signature `CErrorOutputWindow(EFlag)`) replaces it. Its vtable
// is weak (inline dtor), and a zero-filled `_ZTV18CErrorOutputWindow` stub here silently won
// the link with no `multiple definition` line - `tools/restub_reach.py --objdir` now retires
// stubs that shadow weak definitions.

// CFrustumPlanes::CFrustumPlanes()
extern "C" void reachstub_41() asm("_ZN14CFrustumPlanesC1Ev");
extern "C" void reachstub_41() { mpReachStub("_ZN14CFrustumPlanesC1Ev", "CFrustumPlanes::CFrustumPlanes()"); }

// CGameArchitectureSupport::UnloadAudio()
extern "C" void reachstub_42() asm("_ZN24CGameArchitectureSupport11UnloadAudioEv");
extern "C" void reachstub_42() { mpReachStub("_ZN24CGameArchitectureSupport11UnloadAudioEv", "CGameArchitectureSupport::UnloadAudio()"); }

// CGameArea::fn_80057550() const
extern "C" void reachstub_43() asm("_ZNK9CGameArea11fn_80057550Ev");
extern "C" void reachstub_43() { mpReachStub("_ZNK9CGameArea11fn_80057550Ev", "CGameArea::fn_80057550() const"); }

// CGameArea::fn_800575BC(CStateManager&)
extern "C" void reachstub_44() asm("_ZN9CGameArea11fn_800575BCER13CStateManager");
extern "C" void reachstub_44() { mpReachStub("_ZN9CGameArea11fn_800575BCER13CStateManager", "CGameArea::fn_800575BC(CStateManager&)"); }

// CGameCollision::RayStaticIntersection(CStateManager const&, CVector3f const&, CVector3f const&, float, CMaterialFilter const&)
extern "C" void reachstub_45() asm("_ZN14CGameCollision21RayStaticIntersectionERK13CStateManagerRK9CVector3fS5_fRK15CMaterialFilter");
extern "C" void reachstub_45() { mpReachStub("_ZN14CGameCollision21RayStaticIntersectionERK13CStateManagerRK9CVector3fS5_fRK15CMaterialFilter", "CGameCollision::RayStaticIntersection(CStateManager const&, CVector3f const&, CVector3f const&, float, CMaterialFilter const&)"); }

// CGameOptions::~CGameOptions()
extern "C" void reachstub_47() asm("_ZN12CGameOptionsD1Ev");
extern "C" void reachstub_47() { mpReachStub("_ZN12CGameOptionsD1Ev", "CGameOptions::~CGameOptions()"); }

// CGameState::CGameState(CInputStream&, int)
extern "C" void reachstub_48() asm("_ZN10CGameStateC1ER12CInputStreami");
extern "C" void reachstub_48() { mpReachStub("_ZN10CGameStateC1ER12CInputStreami", "CGameState::CGameState(CInputStream&, int)"); }

// CGameState::GetHardModeEnabled() const
extern "C" void reachstub_50() asm("_ZNK10CGameState18GetHardModeEnabledEv");
extern "C" void reachstub_50() { mpReachStub("_ZNK10CGameState18GetHardModeEnabledEv", "CGameState::GetHardModeEnabled() const"); }

// CGameState::SetUnk50(float)
extern "C" void reachstub_51() asm("_ZN10CGameState8SetUnk50Ef");
extern "C" void reachstub_51() { mpReachStub("_ZN10CGameState8SetUnk50Ef", "CGameState::SetUnk50(float)"); }

// RETIRED 2026-09-27 (lane step12). `CGraphics::SetModelMatrix` now has a real definition:
// `src/Kyoto/Graphics/Carve802C24AC.cpp` (Matching 100.00%) plus the `fn_802C2614` it
// relocates against, `src/Kyoto/Graphics/Carve802C2614.c`. Both are listed in `files.cmake`
// as of this change. Delete the alias here; do NOT remove the decomp unit.

// RETIRED 2026-09-26. A reach stub aliased onto a symbol that now has a real definition, so
// listing the decomp unit made the two collide - and the collision is invisible to the gate,
// because this file is added by CMakeLists.txt only under `-DMP_BOOT_STUBS=ON`, which only
// tools/boot_probe.sh passes. `gate.sh`'s `port link dups` step runs without the option, so
// `duplicate definitions 0` is a true statement about a build in which this file is absent.
// tools/boot_probe.sh now counts `multiple definition of` lines and names the symbol. Delete
// the alias here; do NOT remove the decomp unit.

// RETIRED 2026-09-26. A reach stub aliased onto a symbol that now has a real definition; see
// the note on `CActor::SetDirtyFlags` above for why the gate cannot see this class of collision.

// CGrappleArm::fn_801C37BC(int)
extern "C" void reachstub_56() asm("_ZN11CGrappleArm11fn_801C37BCEi");
extern "C" void reachstub_56() { mpReachStub("_ZN11CGrappleArm11fn_801C37BCEi", "CGrappleArm::fn_801C37BC(int)"); }

// CGrappleArm::fn_801C3824(CStateManager&, float, bool)
extern "C" void reachstub_57() asm("_ZN11CGrappleArm11fn_801C3824ER13CStateManagerfb");
extern "C" void reachstub_57() { mpReachStub("_ZN11CGrappleArm11fn_801C3824ER13CStateManagerfb", "CGrappleArm::fn_801C3824(CStateManager&, float, bool)"); }

// CGunEffectUnk::fn_801DCFF0()
extern "C" void reachstub_58() asm("_ZN13CGunEffectUnk11fn_801DCFF0Ev");
extern "C" void reachstub_58() { mpReachStub("_ZN13CGunEffectUnk11fn_801DCFF0Ev", "CGunEffectUnk::fn_801DCFF0()"); }

// CGunEffectUnk::fn_801DD010()
extern "C" void reachstub_59() asm("_ZN13CGunEffectUnk11fn_801DD010Ev");
extern "C" void reachstub_59() { mpReachStub("_ZN13CGunEffectUnk11fn_801DD010Ev", "CGunEffectUnk::fn_801DD010()"); }

// CGunStateMachine::GetCurrentStateName() const
extern "C" void reachstub_60() asm("_ZNK16CGunStateMachine19GetCurrentStateNameEv");
extern "C" void reachstub_60() { mpReachStub("_ZNK16CGunStateMachine19GetCurrentStateNameEv", "CGunStateMachine::GetCurrentStateName() const"); }

// CGunStateMachine::SetState(CStateManager&, CPlayerGun*, rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator> const&)
extern "C" void reachstub_61() asm("_ZN16CGunStateMachine8SetStateER13CStateManagerP10CPlayerGunRKN4rstl12basic_stringIcNS4_11char_traitsIcEENS4_17rmemory_allocatorEEE");
extern "C" void reachstub_61() { mpReachStub("_ZN16CGunStateMachine8SetStateER13CStateManagerP10CPlayerGunRKN4rstl12basic_stringIcNS4_11char_traitsIcEENS4_17rmemory_allocatorEEE", "CGunStateMachine::SetState(CStateManager&, CPlayerGun*, rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator> const&)"); }

// CGunStateMachine::SetStateFuncs(SGunStateFunc const*, int)
extern "C" void reachstub_62() asm("_ZN16CGunStateMachine13SetStateFuncsEPK13SGunStateFunci");
extern "C" void reachstub_62() { mpReachStub("_ZN16CGunStateMachine13SetStateFuncsEPK13SGunStateFunci", "CGunStateMachine::SetStateFuncs(SGunStateFunc const*, int)"); }

// CGunStateMachine::SetStateMachine(CStateMachine const*)
extern "C" void reachstub_63() asm("_ZN16CGunStateMachine15SetStateMachineEPK13CStateMachine");
extern "C" void reachstub_63() { mpReachStub("_ZN16CGunStateMachine15SetStateMachineEPK13CStateMachine", "CGunStateMachine::SetStateMachine(CStateMachine const*)"); }

// CGunStateMachine::SetTriggerFuncs(SGunTriggerFunc const*, int)
extern "C" void reachstub_64() asm("_ZN16CGunStateMachine15SetTriggerFuncsEPK15SGunTriggerFunci");
extern "C" void reachstub_64() { mpReachStub("_ZN16CGunStateMachine15SetTriggerFuncsEPK15SGunTriggerFunci", "CGunStateMachine::SetTriggerFuncs(SGunTriggerFunc const*, int)"); }

// CGunWeapon::fn_801D8EC0()
extern "C" void reachstub_67() asm("_ZN10CGunWeapon11fn_801D8EC0Ev");
extern "C" void reachstub_67() { mpReachStub("_ZN10CGunWeapon11fn_801D8EC0Ev", "CGunWeapon::fn_801D8EC0()"); }

// CGunWeapon::fn_801D8F2C()
extern "C" void reachstub_68() asm("_ZN10CGunWeapon11fn_801D8F2CEv");
extern "C" void reachstub_68() { mpReachStub("_ZN10CGunWeapon11fn_801D8F2CEv", "CGunWeapon::fn_801D8F2C()"); }

// CGunWeapon::fn_801D8F64()
extern "C" void reachstub_69() asm("_ZN10CGunWeapon11fn_801D8F64Ev");
extern "C" void reachstub_69() { mpReachStub("_ZN10CGunWeapon11fn_801D8F64Ev", "CGunWeapon::fn_801D8F64()"); }

// CGunWeapon::fn_801DA364(CStateManager&, bool)
extern "C" void reachstub_70() asm("_ZN10CGunWeapon11fn_801DA364ER13CStateManagerb");
extern "C" void reachstub_70() { mpReachStub("_ZN10CGunWeapon11fn_801DA364ER13CStateManagerb", "CGunWeapon::fn_801DA364(CStateManager&, bool)"); }

// `reachstub_71` (CIOWinManager::RemoveIOWin) deleted: `src/MetroidPrime/CIOWinManager.cpp`
// is in `files.cmake` and defines `RemoveIOWin` for real, so the alias is a duplicate the boot probe reports and the gate's `port link
// dups` step cannot see (this file is compiled only under `-DMP_BOOT_STUBS=ON`).
// `reachstub_297` (fn_80193E08) is the opposite case and was put back - see below.
//
// CInGameTweakManager::GetIdentifierForMusicEvent(unsigned int, rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator> const&)
extern "C" void reachstub_72() asm("_ZN19CInGameTweakManager26GetIdentifierForMusicEventEjRKN4rstl12basic_stringIcNS0_11char_traitsIcEENS0_17rmemory_allocatorEEE");
extern "C" void reachstub_72() { mpReachStub("_ZN19CInGameTweakManager26GetIdentifierForMusicEventEjRKN4rstl12basic_stringIcNS0_11char_traitsIcEENS0_17rmemory_allocatorEEE", "CInGameTweakManager::GetIdentifierForMusicEvent(unsigned int, rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator> const&)"); }

// CInGameTweakManager::GetTweakValue(rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator> const&) const
extern "C" void reachstub_73() asm("_ZNK19CInGameTweakManager13GetTweakValueERKN4rstl12basic_stringIcNS0_11char_traitsIcEENS0_17rmemory_allocatorEEE");
extern "C" void reachstub_73() { mpReachStub("_ZNK19CInGameTweakManager13GetTweakValueERKN4rstl12basic_stringIcNS0_11char_traitsIcEENS0_17rmemory_allocatorEEE", "CInGameTweakManager::GetTweakValue(rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator> const&) const"); }

// CInGameTweakManager::HasTweakValue(rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator> const&) const
extern "C" void reachstub_74() asm("_ZNK19CInGameTweakManager13HasTweakValueERKN4rstl12basic_stringIcNS0_11char_traitsIcEENS0_17rmemory_allocatorEEE");
extern "C" void reachstub_74() { mpReachStub("_ZNK19CInGameTweakManager13HasTweakValueERKN4rstl12basic_stringIcNS0_11char_traitsIcEENS0_17rmemory_allocatorEEE", "CInGameTweakManager::HasTweakValue(rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator> const&) const"); }

// CInputGenerator::Update(float, CArchitectureQueue&)

// CMain::ResetGameState()
extern "C" void reachstub_78() asm("_ZN5CMain14ResetGameStateEv");
extern "C" void reachstub_78() { mpReachStub("_ZN5CMain14ResetGameStateEv", "CMain::ResetGameState()"); }

// CModelData::AdvanceAnimation(float, CStateManager&, TAreaId, bool)
extern "C" void reachstub_79() asm("_ZN10CModelData16AdvanceAnimationEfR13CStateManager7TAreaIdb");
extern "C" void reachstub_79() { mpReachStub("_ZN10CModelData16AdvanceAnimationEfR13CStateManager7TAreaIdb", "CModelData::AdvanceAnimation(float, CStateManager&, TAreaId, bool)"); }

// CModelData::SetInfraModel(rstl::pair<unsigned int, unsigned int> const&)
extern "C" void reachstub_90() asm("_ZN10CModelData13SetInfraModelERKN4rstl4pairIjjEE");
extern "C" void reachstub_90() { mpReachStub("_ZN10CModelData13SetInfraModelERKN4rstl4pairIjjEE", "CModelData::SetInfraModel(rstl::pair<unsigned int, unsigned int> const&)"); }

// CModelData::SetXRayModel(rstl::pair<unsigned int, unsigned int> const&)
extern "C" void reachstub_91() asm("_ZN10CModelData12SetXRayModelERKN4rstl4pairIjjEE");
extern "C" void reachstub_91() { mpReachStub("_ZN10CModelData12SetXRayModelERKN4rstl4pairIjjEE", "CModelData::SetXRayModel(rstl::pair<unsigned int, unsigned int> const&)"); }

// RETIRED 2026-09-28. src/MetroidPrime/CModelDataDtor.cpp defines `CModelData::~CModelData()`
// for real and is in files.cmake, so this alias is a duplicate under -DMP_BOOT_STUBS=ON - the
// only configuration tools/boot_probe.sh builds, and the one gate.sh's 'port link dups' step
// cannot see. Same shape as the AllocateRenderer alias above; re-running the generator over an
// unfixed docs/research/boot_path_reachable.tsv puts it back.

// CObjectList::fn_8000B538(TUniqueId) const
extern "C" void reachstub_94() asm("_ZNK11CObjectList11fn_8000B538E9TUniqueId");
extern "C" void reachstub_94() { mpReachStub("_ZNK11CObjectList11fn_8000B538E9TUniqueId", "CObjectList::fn_8000B538(TUniqueId) const"); }

// CObjectList::fn_8000B588(TUniqueId)
extern "C" void reachstub_95() asm("_ZN11CObjectList11fn_8000B588E9TUniqueId");
extern "C" void reachstub_95() { mpReachStub("_ZN11CObjectList11fn_8000B588E9TUniqueId", "CObjectList::fn_8000B588(TUniqueId)"); }

// CParticleDatabase::DeleteAllLights(CStateManager&)
extern "C" void reachstub_96() asm("_ZN17CParticleDatabase15DeleteAllLightsER13CStateManager");
extern "C" void reachstub_96() { mpReachStub("_ZN17CParticleDatabase15DeleteAllLightsER13CStateManager", "CParticleDatabase::DeleteAllLights(CStateManager&)"); }

// CParticleDatabase::GetBounds() const
extern "C" void reachstub_97() asm("_ZNK17CParticleDatabase9GetBoundsEv");
extern "C" void reachstub_97() { mpReachStub("_ZNK17CParticleDatabase9GetBoundsEv", "CParticleDatabase::GetBounds() const"); }

// CPlayer* TCastToPtr<CPlayer>(CEntity&)
extern "C" void reachstub_100() asm("_Z10TCastToPtrI7CPlayerEPT_R7CEntity");
extern "C" void reachstub_100() { mpReachStub("_Z10TCastToPtrI7CPlayerEPT_R7CEntity", "CPlayer* TCastToPtr<CPlayer>(CEntity&)"); }

// CPlayer* TCastToPtr<CPlayer>(CEntity*)
extern "C" void reachstub_101() asm("_Z10TCastToPtrI7CPlayerEPT_P7CEntity");
extern "C" void reachstub_101() { mpReachStub("_Z10TCastToPtrI7CPlayerEPT_P7CEntity", "CPlayer* TCastToPtr<CPlayer>(CEntity*)"); }

// CPlayer::PlaySfxForPlayer(unsigned int, short, int, bool, int)
extern "C" void reachstub_102() asm("_ZN7CPlayer16PlaySfxForPlayerEjsibi");
extern "C" void reachstub_102() { mpReachStub("_ZN7CPlayer16PlaySfxForPlayerEjsibi", "CPlayer::PlaySfxForPlayer(unsigned int, short, int, bool, int)"); }

// CPlayer::fn_8000BE98() const
extern "C" void reachstub_104() asm("_ZNK7CPlayer11fn_8000BE98Ev");
extern "C" void reachstub_104() { mpReachStub("_ZNK7CPlayer11fn_8000BE98Ev", "CPlayer::fn_8000BE98() const"); }

// CPlayerGun::ComboActive(CStateManager&, EStateMsg, float)
extern "C" void reachstub_107() asm("_ZN10CPlayerGun11ComboActiveER13CStateManager9EStateMsgf");
extern "C" void reachstub_107() { mpReachStub("_ZN10CPlayerGun11ComboActiveER13CStateManager9EStateMsgf", "CPlayerGun::ComboActive(CStateManager&, EStateMsg, float)"); }

// CPlayerGun::FidgetOver(CStateManager&, CTriggerData const&)
extern "C" void reachstub_109() asm("_ZN10CPlayerGun10FidgetOverER13CStateManagerRK12CTriggerData");
extern "C" void reachstub_109() { mpReachStub("_ZN10CPlayerGun10FidgetOverER13CStateManagerRK12CTriggerData", "CPlayerGun::FidgetOver(CStateManager&, CTriggerData const&)"); }

// CPlayerGun::Fidgeting(CStateManager&, EStateMsg, float)
extern "C" void reachstub_110() asm("_ZN10CPlayerGun9FidgetingER13CStateManager9EStateMsgf");
extern "C" void reachstub_110() { mpReachStub("_ZN10CPlayerGun9FidgetingER13CStateManager9EStateMsgf", "CPlayerGun::Fidgeting(CStateManager&, EStateMsg, float)"); }

// CPlayerGun::GetPlayer(CStateManager&) const
extern "C" void reachstub_112() asm("_ZNK10CPlayerGun9GetPlayerER13CStateManager");
extern "C" void reachstub_112() { mpReachStub("_ZNK10CPlayerGun9GetPlayerER13CStateManager", "CPlayerGun::GetPlayer(CStateManager&) const"); }

// CPlayerGun::GetPlayerFromAll(CStateManager&) const
extern "C" void reachstub_113() asm("_ZNK10CPlayerGun16GetPlayerFromAllER13CStateManager");
extern "C" void reachstub_113() { mpReachStub("_ZNK10CPlayerGun16GetPlayerFromAllER13CStateManager", "CPlayerGun::GetPlayerFromAll(CStateManager&) const"); }

// CPlayerGun::InPhazon(CStateManager&, CTriggerData const&)
extern "C" void reachstub_115() asm("_ZN10CPlayerGun8InPhazonER13CStateManagerRK12CTriggerData");
extern "C" void reachstub_115() { mpReachStub("_ZN10CPlayerGun8InPhazonER13CStateManagerRK12CTriggerData", "CPlayerGun::InPhazon(CStateManager&, CTriggerData const&)"); }

// CPlayerGun::InitiateCombo(CStateManager&, CTriggerData const&)
extern "C" void reachstub_116() asm("_ZN10CPlayerGun13InitiateComboER13CStateManagerRK12CTriggerData");
extern "C" void reachstub_116() { mpReachStub("_ZN10CPlayerGun13InitiateComboER13CStateManagerRK12CTriggerData", "CPlayerGun::InitiateCombo(CStateManager&, CTriggerData const&)"); }

// CPlayerGun::Main(CStateManager&, EStateMsg, float)
extern "C" void reachstub_118() asm("_ZN10CPlayerGun4MainER13CStateManager9EStateMsgf");
extern "C" void reachstub_118() { mpReachStub("_ZN10CPlayerGun4MainER13CStateManager9EStateMsgf", "CPlayerGun::Main(CStateManager&, EStateMsg, float)"); }

// CPlayerGun::PlayAnim(CStateManager&, int, int)
extern "C" void reachstub_119() asm("_ZN10CPlayerGun8PlayAnimER13CStateManagerii");
extern "C" void reachstub_119() { mpReachStub("_ZN10CPlayerGun8PlayAnimER13CStateManagerii", "CPlayerGun::PlayAnim(CStateManager&, int, int)"); }

// CPlayerGun::Recoil(CStateManager&, EStateMsg, float)
extern "C" void reachstub_120() asm("_ZN10CPlayerGun6RecoilER13CStateManager9EStateMsgf");
extern "C" void reachstub_120() { mpReachStub("_ZN10CPlayerGun6RecoilER13CStateManager9EStateMsgf", "CPlayerGun::Recoil(CStateManager&, EStateMsg, float)"); }

// CPlayerGun::fn_801C72B4(CStateManager&, float)
extern "C" void reachstub_123() asm("_ZN10CPlayerGun11fn_801C72B4ER13CStateManagerf");
extern "C" void reachstub_123() { mpReachStub("_ZN10CPlayerGun11fn_801C72B4ER13CStateManagerf", "CPlayerGun::fn_801C72B4(CStateManager&, float)"); }

// CPlayerGun::fn_801CA734(CStateManager&)
extern "C" void reachstub_124() asm("_ZN10CPlayerGun11fn_801CA734ER13CStateManager");
extern "C" void reachstub_124() { mpReachStub("_ZN10CPlayerGun11fn_801CA734ER13CStateManager", "CPlayerGun::fn_801CA734(CStateManager&)"); }

// CPlayerGun::fn_801CD55C(CStateManager&, bool)
extern "C" void reachstub_125() asm("_ZN10CPlayerGun11fn_801CD55CER13CStateManagerb");
extern "C" void reachstub_125() { mpReachStub("_ZN10CPlayerGun11fn_801CD55CER13CStateManagerb", "CPlayerGun::fn_801CD55C(CStateManager&, bool)"); }

// CPlayerGun::fn_801CE0DC(CStateManager&)
extern "C" void reachstub_126() asm("_ZN10CPlayerGun11fn_801CE0DCER13CStateManager");
extern "C" void reachstub_126() { mpReachStub("_ZN10CPlayerGun11fn_801CE0DCER13CStateManager", "CPlayerGun::fn_801CE0DC(CStateManager&)"); }

// CPlayerGun::fn_801CE5C0(CFinalInput const&, CStateManager&)
extern "C" void reachstub_127() asm("_ZN10CPlayerGun11fn_801CE5C0ERK11CFinalInputR13CStateManager");
extern "C" void reachstub_127() { mpReachStub("_ZN10CPlayerGun11fn_801CE5C0ERK11CFinalInputR13CStateManager", "CPlayerGun::fn_801CE5C0(CFinalInput const&, CStateManager&)"); }

// CPlayerGun::fn_801DE430(CStateManager&)
extern "C" void reachstub_128() asm("_ZN10CPlayerGun11fn_801DE430ER13CStateManager");
extern "C" void reachstub_128() { mpReachStub("_ZN10CPlayerGun11fn_801DE430ER13CStateManager", "CPlayerGun::fn_801DE430(CStateManager&)"); }

// CPlayerGunUnk570::fn_801D6D8C()
extern "C" void reachstub_129() asm("_ZN16CPlayerGunUnk57011fn_801D6D8CEv");
extern "C" void reachstub_129() { mpReachStub("_ZN16CPlayerGunUnk57011fn_801D6D8CEv", "CPlayerGunUnk570::fn_801D6D8C()"); }

// CPlayerGunUnk570::fn_801D6ED0(int, CStateManager&, float, bool)
extern "C" void reachstub_130() asm("_ZN16CPlayerGunUnk57011fn_801D6ED0EiR13CStateManagerfb");
extern "C" void reachstub_130() { mpReachStub("_ZN16CPlayerGunUnk57011fn_801D6ED0EiR13CStateManagerfb", "CPlayerGunUnk570::fn_801D6ED0(int, CStateManager&, float, bool)"); }

// CPlayerGunUnk578::fn_801D5DD0(int, CStateManager&)
extern "C" void reachstub_131() asm("_ZN16CPlayerGunUnk57811fn_801D5DD0EiR13CStateManager");
extern "C" void reachstub_131() { mpReachStub("_ZN16CPlayerGunUnk57811fn_801D5DD0EiR13CStateManager", "CPlayerGunUnk578::fn_801D5DD0(int, CStateManager&)"); }

// CPlayerGunUnk578::fn_801D6894(CStateManager&, bool)
extern "C" void reachstub_132() asm("_ZN16CPlayerGunUnk57811fn_801D6894ER13CStateManagerb");
extern "C" void reachstub_132() { mpReachStub("_ZN16CPlayerGunUnk57811fn_801D6894ER13CStateManagerb", "CPlayerGunUnk578::fn_801D6894(CStateManager&, bool)"); }

// CPlayerGunUnk578::fn_801D6924() const
extern "C" void reachstub_133() asm("_ZNK16CPlayerGunUnk57811fn_801D6924Ev");
extern "C" void reachstub_133() { mpReachStub("_ZNK16CPlayerGunUnk57811fn_801D6924Ev", "CPlayerGunUnk578::fn_801D6924() const"); }

// CPlayerGunUnk578::fn_801D6930(TUniqueId)
extern "C" void reachstub_134() asm("_ZN16CPlayerGunUnk57811fn_801D6930E9TUniqueId");
extern "C" void reachstub_134() { mpReachStub("_ZN16CPlayerGunUnk57811fn_801D6930E9TUniqueId", "CPlayerGunUnk578::fn_801D6930(TUniqueId)"); }

// CPlayerGunUnk624::fn_80320978()
extern "C" void reachstub_135() asm("_ZN16CPlayerGunUnk62411fn_80320978Ev");
extern "C" void reachstub_135() { mpReachStub("_ZN16CPlayerGunUnk62411fn_80320978Ev", "CPlayerGunUnk624::fn_80320978()"); }

// CPlayerGunUnk624::fn_80320A04()
extern "C" void reachstub_136() asm("_ZN16CPlayerGunUnk62411fn_80320A04Ev");
extern "C" void reachstub_136() { mpReachStub("_ZN16CPlayerGunUnk62411fn_80320A04Ev", "CPlayerGunUnk624::fn_80320A04()"); }

// CRumbleManager::Rumble(CStateManager&, ERumbleFxId, float, ERumblePriority)
extern "C" void reachstub_138() asm("_ZN14CRumbleManager6RumbleER13CStateManager11ERumbleFxIdf15ERumblePriority");
extern "C" void reachstub_138() { mpReachStub("_ZN14CRumbleManager6RumbleER13CStateManager11ERumbleFxIdf15ERumblePriority", "CRumbleManager::Rumble(CStateManager&, ERumbleFxId, float, ERumblePriority)"); }

// CSaveGameScreen::CSaveGameScreen(int, unsigned long)
extern "C" void reachstub_140() asm("_ZN15CSaveGameScreenC1Eim");
extern "C" void reachstub_140() { mpReachStub("_ZN15CSaveGameScreenC1Eim", "CSaveGameScreen::CSaveGameScreen(int, unsigned long)"); }

// CSaveGameScreen::~CSaveGameScreen()
extern "C" void reachstub_141() asm("_ZN15CSaveGameScreenD1Ev");
extern "C" void reachstub_141() { mpReachStub("_ZN15CSaveGameScreenD1Ev", "CSaveGameScreen::~CSaveGameScreen()"); }

// CScriptEffect* TCastToPtr<CScriptEffect>(CEntity*)
extern "C" void reachstub_142() asm("_Z10TCastToPtrI13CScriptEffectEPT_P7CEntity");
extern "C" void reachstub_142() { mpReachStub("_Z10TCastToPtrI13CScriptEffectEPT_P7CEntity", "CScriptEffect* TCastToPtr<CScriptEffect>(CEntity*)"); }

// CScriptEffect::CScriptEffect(TUniqueId, rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator> const&, CEntityInfo const&, CTransform4f const&, CVector3f const&, unsigned int, int, int, int, int, float, float, float, float, bool, float, float, float, bool, bool, bool, CLightParameters const&, bool, CScriptEffect::ParamStruct const&, bool, bool, bool, int)
extern "C" void reachstub_143() asm("_ZN13CScriptEffectC1E9TUniqueIdRKN4rstl12basic_stringIcNS1_11char_traitsIcEENS1_17rmemory_allocatorEEERK11CEntityInfoRK12CTransform4fRK9CVector3fjiiiiffffbfffbbbRK16CLightParametersbRKNS_11ParamStructEbbbi");
extern "C" void reachstub_143() { mpReachStub("_ZN13CScriptEffectC1E9TUniqueIdRKN4rstl12basic_stringIcNS1_11char_traitsIcEENS1_17rmemory_allocatorEEERK11CEntityInfoRK12CTransform4fRK9CVector3fjiiiiffffbfffbbbRK16CLightParametersbRKNS_11ParamStructEbbbi", "CScriptEffect::CScriptEffect(TUniqueId, rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator> const&, CEntityInfo const&, CTransform4f const&, CVector3f const&, unsigned int, int, int, int, int, float, float, float, float, bool, float, float, float, bool, bool, bool, CLightParameters const&, bool, CScriptEffect::ParamStruct const&, bool, bool, bool, int)"); }

// CSfxManager::AddEmitter(CAudioSys::C3DEmitterParmData&, bool, short, bool, int)
extern "C" void reachstub_144() asm("_ZN11CSfxManager10AddEmitterERN9CAudioSys18C3DEmitterParmDataEbsbi");
extern "C" void reachstub_144() { mpReachStub("_ZN11CSfxManager10AddEmitterERN9CAudioSys18C3DEmitterParmDataEbsbi", "CSfxManager::AddEmitter(CAudioSys::C3DEmitterParmData&, bool, short, bool, int)"); }

// CSfxManager::SfxStart(unsigned short, short, short, bool, short, bool, int)
extern "C" void reachstub_147() asm("_ZN11CSfxManager8SfxStartEtssbsbi");
extern "C" void reachstub_147() { mpReachStub("_ZN11CSfxManager8SfxStartEtssbsbi", "CSfxManager::SfxStart(unsigned short, short, short, bool, short, bool, int)"); }

// RETIRED 2026-09-28. `src/MetroidPrime/PortAudio.cpp` defines
// `_ZN11CSfxManager14TranslateSFXIDEt` for real (retail `fn_8029C79C`, 0x4C,
// `CSfxManager::TranslateSFXID`), so this alias is a duplicate under MP_BOOT_STUBS=ON - the
// configuration only tools/boot_probe.sh uses, and the one gate.sh's duplicate count cannot
// see. `tools/boot_probe.sh`'s own duplicate-definition branch prescribes exactly this: delete
// the stale alias, not the definition. `docs/research/boot_path_undefined.txt` and
// `boot_path_reachable.tsv` still list the symbol, so re-running the generator here puts the
// alias back until those two are regenerated.

// RETIRED 2026-09-27. `src/Kyoto/CSimplePoolPort.cpp` defines `_ZN11CSimplePool11fn_8029c7e8ERK10SObjectTag`
// for real (retail 0x8029C7E8, `CSfxManager::LoadTranslationTable`), so this alias is a duplicate
// under MP_BOOT_STUBS=ON - the configuration only tools/boot_probe.sh uses, and the one gate.sh's
// duplicate count cannot see. `tools/boot_probe.sh`'s own duplicate-definition branch prescribes
// exactly this: delete the stale alias, not the definition. `docs/research/boot_path_reachable.tsv`
// still lists the symbol, so re-running the generator here puts the alias back.

// CSortedListManager::BuildColliderList(rstl::reserved_vector< TUniqueId, kMaxObjects >&, CActor const&, CAABox const&) const
extern "C" void reachstub_151() asm("_ZNK18CSortedListManager17BuildColliderListERN4rstl15reserved_vectorI9TUniqueIdLi1024EEERK6CActorRK6CAABox");
extern "C" void reachstub_151() { mpReachStub("_ZNK18CSortedListManager17BuildColliderListERN4rstl15reserved_vectorI9TUniqueIdLi1024EEERK6CActorRK6CAABox", "CSortedListManager::BuildColliderList(rstl::reserved_vector< TUniqueId, kMaxObjects >&, CActor const&, CAABox const&) const"); }

// CSortedListManager::BuildNearList(rstl::reserved_vector< TUniqueId, kMaxObjects >&, CAABox const&, CMaterialFilter const&, CActor const*) const
extern "C" void reachstub_152() asm("_ZNK18CSortedListManager13BuildNearListERN4rstl15reserved_vectorI9TUniqueIdLi1024EEERK6CAABoxRK15CMaterialFilterPK6CActor");
extern "C" void reachstub_152() { mpReachStub("_ZNK18CSortedListManager13BuildNearListERN4rstl15reserved_vectorI9TUniqueIdLi1024EEERK6CAABoxRK15CMaterialFilterPK6CActor", "CSortedListManager::BuildNearList(rstl::reserved_vector< TUniqueId, kMaxObjects >&, CAABox const&, CMaterialFilter const&, CActor const*) const"); }

// CSortedListManager::BuildNearList(rstl::reserved_vector< TUniqueId, kMaxObjects >&, CVector3f const&, CVector3f const&, float, CMaterialFilter const&, CActor const*) const
extern "C" void reachstub_153() asm("_ZNK18CSortedListManager13BuildNearListERN4rstl15reserved_vectorI9TUniqueIdLi1024EEERK9CVector3fS7_fRK15CMaterialFilterPK6CActor");
extern "C" void reachstub_153() { mpReachStub("_ZNK18CSortedListManager13BuildNearListERN4rstl15reserved_vectorI9TUniqueIdLi1024EEERK9CVector3fS7_fRK15CMaterialFilterPK6CActor", "CSortedListManager::BuildNearList(rstl::reserved_vector< TUniqueId, kMaxObjects >&, CVector3f const&, CVector3f const&, float, CMaterialFilter const&, CActor const*) const"); }

// CStateManager::AddObject(CEntity&)
extern "C" void reachstub_154() asm("_ZN13CStateManager9AddObjectER7CEntity");
extern "C" void reachstub_154() { mpReachStub("_ZN13CStateManager9AddObjectER7CEntity", "CStateManager::AddObject(CEntity&)"); }

// CStateManager::DisplayAlertAboutOutOfAmmo(CPlayer const&, CPlayerState::EItemType) const
extern "C" void reachstub_155() asm("_ZNK13CStateManager26DisplayAlertAboutOutOfAmmoERK7CPlayerN12CPlayerState9EItemTypeE");
extern "C" void reachstub_155() { mpReachStub("_ZNK13CStateManager26DisplayAlertAboutOutOfAmmoERK7CPlayerN12CPlayerState9EItemTypeE", "CStateManager::DisplayAlertAboutOutOfAmmo(CPlayer const&, CPlayerState::EItemType) const"); }

// CStateManager::GetObjectByIdFromListAll(TUniqueId)
extern "C" void reachstub_156() asm("_ZN13CStateManager24GetObjectByIdFromListAllE9TUniqueId");
extern "C" void reachstub_156() { mpReachStub("_ZN13CStateManager24GetObjectByIdFromListAllE9TUniqueId", "CStateManager::GetObjectByIdFromListAll(TUniqueId)"); }

// CStateManager::RayCollideWorldInternal(CVector3f const&, CVector3f const&, CMaterialFilter const&, rstl::reserved_vector< TUniqueId, kMaxObjects > const&, CActor const*) const
extern "C" void reachstub_157() asm("_ZNK13CStateManager23RayCollideWorldInternalERK9CVector3fS2_RK15CMaterialFilterRKN4rstl15reserved_vectorI9TUniqueIdLi1024EEEPK6CActor");
extern "C" void reachstub_157() { mpReachStub("_ZNK13CStateManager23RayCollideWorldInternalERK9CVector3fS2_RK15CMaterialFilterRKN4rstl15reserved_vectorI9TUniqueIdLi1024EEEPK6CActor", "CStateManager::RayCollideWorldInternal(CVector3f const&, CVector3f const&, CMaterialFilter const&, rstl::reserved_vector< TUniqueId, kMaxObjects > const&, CActor const*) const"); }

// CStateManager::UpdateObjectInLists(CEntity&)
extern "C" void reachstub_159() asm("_ZN13CStateManager19UpdateObjectInListsER7CEntity");
extern "C" void reachstub_159() { mpReachStub("_ZN13CStateManager19UpdateObjectInListsER7CEntity", "CStateManager::UpdateObjectInLists(CEntity&)"); }

// CStateManager::fn_800366e4(CActor*)
extern "C" void reachstub_160() asm("_ZN13CStateManager11fn_800366e4EP6CActor");
extern "C" void reachstub_160() { mpReachStub("_ZN13CStateManager11fn_800366e4EP6CActor", "CStateManager::fn_800366e4(CActor*)"); }

// CStateManager::fn_8003C4B8(CVector3f const&, int)
extern "C" void reachstub_161() asm("_ZN13CStateManager11fn_8003C4B8ERK9CVector3fi");
extern "C" void reachstub_161() { mpReachStub("_ZN13CStateManager11fn_8003C4B8ERK9CVector3fi", "CStateManager::fn_8003C4B8(CVector3f const&, int)"); }

// CStateManager::fn_8003dd88(CActor&, TUniqueId, CDamageInfo const&, bool, int)
extern "C" void reachstub_162() asm("_ZN13CStateManager11fn_8003dd88ER6CActor9TUniqueIdRK11CDamageInfobi");
extern "C" void reachstub_162() { mpReachStub("_ZN13CStateManager11fn_8003dd88ER6CActor9TUniqueIdRK11CDamageInfobi", "CStateManager::fn_8003dd88(CActor&, TUniqueId, CDamageInfo const&, bool, int)"); }

// CStateManager::fn_800412EC(TUniqueId)
extern "C" void reachstub_163() asm("_ZN13CStateManager11fn_800412ECE9TUniqueId");
extern "C" void reachstub_163() { mpReachStub("_ZN13CStateManager11fn_800412ECE9TUniqueId", "CStateManager::fn_800412EC(TUniqueId)"); }

// CStateManager::fn_801EDD8C(TUniqueId) const
extern "C" void reachstub_164() asm("_ZNK13CStateManager11fn_801EDD8CE9TUniqueId");
extern "C" void reachstub_164() { mpReachStub("_ZNK13CStateManager11fn_801EDD8CE9TUniqueId", "CStateManager::fn_801EDD8C(TUniqueId) const"); }

// CStateManagerContainer::~CStateManagerContainer()
extern "C" void reachstub_165() asm("_ZN22CStateManagerContainerD1Ev");
extern "C" void reachstub_165() { mpReachStub("_ZN22CStateManagerContainerD1Ev", "CStateManagerContainer::~CStateManagerContainer()"); }

// CStateManagerUnk2900::~CStateManagerUnk2900()
extern "C" void reachstub_166() asm("_ZN20CStateManagerUnk2900D1Ev");
extern "C" void reachstub_166() { mpReachStub("_ZN20CStateManagerUnk2900D1Ev", "CStateManagerUnk2900::~CStateManagerUnk2900()"); }

// CStreamAudioManager::FadeBackIn(int, float)
extern "C" void reachstub_169() asm("_ZN19CStreamAudioManager10FadeBackInEif");
extern "C" void reachstub_169() { mpReachStub("_ZN19CStreamAudioManager10FadeBackInEif", "CStreamAudioManager::FadeBackIn(int, float)"); }

// CStreamAudioManager::Start(int, rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator> const&, unsigned char, bool, float, float)
extern "C" void reachstub_172() asm("_ZN19CStreamAudioManager5StartEiRKN4rstl12basic_stringIcNS0_11char_traitsIcEENS0_17rmemory_allocatorEEEhbff");
extern "C" void reachstub_172() { mpReachStub("_ZN19CStreamAudioManager5StartEiRKN4rstl12basic_stringIcNS0_11char_traitsIcEENS0_17rmemory_allocatorEEEhbff", "CStreamAudioManager::Start(int, rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator> const&, unsigned char, bool, float, float)"); }

// CStreamAudioManager::Stop(int, rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator> const&)
extern "C" void reachstub_173() asm("_ZN19CStreamAudioManager4StopEiRKN4rstl12basic_stringIcNS0_11char_traitsIcEENS0_17rmemory_allocatorEEE");
extern "C" void reachstub_173() { mpReachStub("_ZN19CStreamAudioManager4StopEiRKN4rstl12basic_stringIcNS0_11char_traitsIcEENS0_17rmemory_allocatorEEE", "CStreamAudioManager::Stop(int, rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator> const&)"); }

// CStreamAudioManager::TemporaryFadeOut(int, float)
extern "C" void reachstub_174() asm("_ZN19CStreamAudioManager16TemporaryFadeOutEif");
extern "C" void reachstub_174() { mpReachStub("_ZN19CStreamAudioManager16TemporaryFadeOutEif", "CStreamAudioManager::TemporaryFadeOut(int, float)"); }

// CStreamAudioManager::sub_803653f8(float)
extern "C" void reachstub_175() asm("_ZN19CStreamAudioManager12sub_803653f8Ef");
extern "C" void reachstub_175() { mpReachStub("_ZN19CStreamAudioManager12sub_803653f8Ef", "CStreamAudioManager::sub_803653f8(float)"); }

// CStreamAudioManager::sub_80365424(float)
extern "C" void reachstub_176() asm("_ZN19CStreamAudioManager12sub_80365424Ef");
extern "C" void reachstub_176() { mpReachStub("_ZN19CStreamAudioManager12sub_80365424Ef", "CStreamAudioManager::sub_80365424(float)"); }

// CStreamAudioManager::sub_8036590c(float)
extern "C" void reachstub_177() asm("_ZN19CStreamAudioManager12sub_8036590cEf");
extern "C" void reachstub_177() { mpReachStub("_ZN19CStreamAudioManager12sub_8036590cEf", "CStreamAudioManager::sub_8036590c(float)"); }

// CTweakGame::GetPakFile()
extern "C" void reachstub_178() asm("_ZN10CTweakGame10GetPakFileEv");
extern "C" void reachstub_178() { mpReachStub("_ZN10CTweakGame10GetPakFileEv", "CTweakGame::GetPakFile()"); }

// CTweakGame::GetTotalPercentage()
extern "C" void reachstub_179() asm("_ZN10CTweakGame18GetTotalPercentageEv");
extern "C" void reachstub_179() { mpReachStub("_ZN10CTweakGame18GetTotalPercentageEv", "CTweakGame::GetTotalPercentage()"); }

// CTweakPlayerGun::GetMaxAbsorbedPhazonShots()
extern "C" void reachstub_180() asm("_ZN15CTweakPlayerGun25GetMaxAbsorbedPhazonShotsEv");
extern "C" void reachstub_180() { mpReachStub("_ZN15CTweakPlayerGun25GetMaxAbsorbedPhazonShotsEv", "CTweakPlayerGun::GetMaxAbsorbedPhazonShots()"); }

// CWeaponMgr::fn_800B321C(TUniqueId, EWeaponType)
extern "C" void reachstub_182() asm("_ZN10CWeaponMgr11fn_800B321CE9TUniqueId11EWeaponType");
extern "C" void reachstub_182() { mpReachStub("_ZN10CWeaponMgr11fn_800B321CE9TUniqueId11EWeaponType", "CWeaponMgr::fn_800B321C(TUniqueId, EWeaponType)"); }

// CWeaponMgr::fn_800B32E0(TUniqueId, EWeaponType)
extern "C" void reachstub_183() asm("_ZN10CWeaponMgr11fn_800B32E0E9TUniqueId11EWeaponType");
extern "C" void reachstub_183() { mpReachStub("_ZN10CWeaponMgr11fn_800B32E0E9TUniqueId11EWeaponType", "CWeaponMgr::fn_800B32E0(TUniqueId, EWeaponType)"); }

// IElement::CElementAllocator::Alloc(unsigned long, char const*, char const*)
extern "C" void reachstub_185() asm("_ZN8IElement17CElementAllocator5AllocEmPKcS2_");
extern "C" void reachstub_185() { mpReachStub("_ZN8IElement17CElementAllocator5AllocEmPKcS2_", "IElement::CElementAllocator::Alloc(unsigned long, char const*, char const*)"); }

// IElement::CElementAllocator::Free(void*, unsigned long)
extern "C" void reachstub_186() asm("_ZN8IElement17CElementAllocator4FreeEPvm");
extern "C" void reachstub_186() { mpReachStub("_ZN8IElement17CElementAllocator4FreeEPvm", "IElement::CElementAllocator::Free(void*, unsigned long)"); }

// RETIRED 2026-09-28. `src/MetroidPrime/LdrToEntityInfo.cpp` defines
// `_Z15LdrToEntityInfoR11CEntityInfoRK20SLdrEditorProperties` and
// `_Z15LdrToEntityInfoRK11CEntityInfoRK20SLdrEditorProperties` for real (retail 0x80239BD4,
// 0x38 - one symbol, the non-const one; the const overload is the port's forwarder over it),
// so both aliases are duplicates under MP_BOOT_STUBS=ON - the configuration only
// tools/boot_probe.sh uses, and the one gate.sh's duplicate count cannot see.
// `tools/boot_probe.sh`'s own duplicate-definition branch prescribes exactly this: delete the
// stale aliases, not the definitions. `docs/research/boot_path_reachable.tsv` still lists both
// symbols, so re-running `tools/gen_link_stubs.py --reachable` here puts them back.

// LdrToActorParameters(SLdrActorParameters const&)
extern "C" void reachstub_189() asm("_Z19LoadActorParametersRK19SLdrActorParameters");
extern "C" void reachstub_189() { mpReachStub("_Z19LoadActorParametersRK19SLdrActorParameters", "LdrToActorParameters(SLdrActorParameters const&)"); }

// LoadCAABox(CStateManager&, TAreaId const&, CVector3f const&, CVector3f const&)
extern "C" void reachstub_190() asm("_Z10LoadCAABoxR13CStateManagerRK7TAreaIdRK9CVector3fS6_");
extern "C" void reachstub_190() { mpReachStub("_Z10LoadCAABoxR13CStateManagerRK7TAreaIdRK9CVector3fS6_", "LoadCAABox(CStateManager&, TAreaId const&, CVector3f const&, CVector3f const&)"); }

// LdrToEchoParameters(SLdrEchoParameters const&)
extern "C" void reachstub_191() asm("_Z18LoadEchoParametersRK18SLdrEchoParameters");
extern "C" void reachstub_191() { mpReachStub("_Z18LoadEchoParametersRK18SLdrEchoParameters", "LdrToEchoParameters(SLdrEchoParameters const&)"); }

// LdrToTransform4f(SLdrEditorProperties const&)
extern "C" void reachstub_192() asm("_Z19LoadEditorTransformRK20SLdrEditorProperties");
extern "C" void reachstub_192() { mpReachStub("_Z19LoadEditorTransformRK20SLdrEditorProperties", "LdrToTransform4f(SLdrEditorProperties const&)"); }

// LoadModelData(CVector3f const&, unsigned int, SLdrAnimationParameters const&, bool)
extern "C" void reachstub_193() asm("_Z13LoadModelDataRK9CVector3fjRK23SLdrAnimationParametersb");
extern "C" void reachstub_193() { mpReachStub("_Z13LoadModelDataRK9CVector3fjRK23SLdrAnimationParametersb", "LoadModelData(CVector3f const&, unsigned int, SLdrAnimationParameters const&, bool)"); }

// RETIRED 2026-09-28. `src/MetroidPrime/ScriptLoader/Structs/SLdrEditorProperties_Load.cpp`
// defines `_Z27LoadTypedefEditorPropertiesR20SLdrEditorPropertiesR12CInputStream` for real
// (retail 0x8023EF3C, 0x140), so this alias is a duplicate under MP_BOOT_STUBS=ON - the
// configuration only tools/boot_probe.sh uses, and the one gate.sh's duplicate count cannot
// see. `tools/boot_probe.sh`'s own duplicate-definition branch prescribes exactly this: delete
// the stale alias, not the definition. `docs/research/boot_path_reachable.tsv` still lists the
// symbol, so re-running `tools/gen_link_stubs.py --reachable` here puts the alias back.

// LoadTypedefActorParameters(SLdrActorParameters&, CInputStream&)
extern "C" void reachstub_195() asm("_Z30LoadTypedefSLdrActorParametersR19SLdrActorParametersR12CInputStream");
extern "C" void reachstub_195() { mpReachStub("_Z30LoadTypedefSLdrActorParametersR19SLdrActorParametersR12CInputStream", "LoadTypedefActorParameters(SLdrActorParameters&, CInputStream&)"); }

// LoadTypedefSLdrAnimationParameters(SLdrAnimationParameters&, CInputStream&)
extern "C" void reachstub_196() asm("_Z34LoadTypedefSLdrAnimationParametersR23SLdrAnimationParametersR12CInputStream");
extern "C" void reachstub_196() { mpReachStub("_Z34LoadTypedefSLdrAnimationParametersR23SLdrAnimationParametersR12CInputStream", "LoadTypedefSLdrAnimationParameters(SLdrAnimationParameters&, CInputStream&)"); }

// LoadTypedefSLdrCameraShakerData(SLdrCameraShakerData&, CInputStream&)
extern "C" void reachstub_197() asm("_Z31LoadTypedefSLdrCameraShakerDataR20SLdrCameraShakerDataR12CInputStream");
extern "C" void reachstub_197() { mpReachStub("_Z31LoadTypedefSLdrCameraShakerDataR20SLdrCameraShakerDataR12CInputStream", "LoadTypedefSLdrCameraShakerData(SLdrCameraShakerData&, CInputStream&)"); }

// LoadTypedefEchoParameters(SLdrEchoParameters&, CInputStream&)
extern "C" void reachstub_198() asm("_Z29LoadTypedefSLdrEchoParametersR18SLdrEchoParametersR12CInputStream");
extern "C" void reachstub_198() { mpReachStub("_Z29LoadTypedefSLdrEchoParametersR18SLdrEchoParametersR12CInputStream", "LoadTypedefEchoParameters(SLdrEchoParameters&, CInputStream&)"); }

// LoadTypedefSLdrPlayerItem(SLdrPlayerItem&, CInputStream&)
extern "C" void reachstub_199() asm("_Z25LoadTypedefSLdrPlayerItemR14SLdrPlayerItemR12CInputStream");
extern "C" void reachstub_199() { mpReachStub("_Z25LoadTypedefSLdrPlayerItemR14SLdrPlayerItemR12CInputStream", "LoadTypedefSLdrPlayerItem(SLdrPlayerItem&, CInputStream&)"); }

// REL_LoadFlyerSwarm
extern "C" void reachstub_260() asm("REL_LoadFlyerSwarm");
extern "C" void reachstub_260() { mpReachStub("REL_LoadFlyerSwarm", "REL_LoadFlyerSwarm"); }

// REL_LoadMetaree
extern "C" void reachstub_261() asm("REL_LoadMetaree");
extern "C" void reachstub_261() { mpReachStub("REL_LoadMetaree", "REL_LoadMetaree"); }

// REL_LoadPuffer
extern "C" void reachstub_262() asm("REL_LoadPuffer");
extern "C" void reachstub_262() { mpReachStub("REL_LoadPuffer", "REL_LoadPuffer"); }

// REL_LoadRiftPortal(CStateManager&, CInputStream&, CEntityInfo const&)
extern "C" void reachstub_263() asm("_Z18REL_LoadRiftPortalR13CStateManagerR12CInputStreamRK11CEntityInfo");
extern "C" void reachstub_263() { mpReachStub("_Z18REL_LoadRiftPortalR13CStateManagerR12CInputStreamRK11CEntityInfo", "REL_LoadRiftPortal(CStateManager&, CInputStream&, CEntityInfo const&)"); }

// RETIRED 2026-09-27. src/MetroidPrime/PortStreamNewGameState.cpp defines this for real and is
// now in files.cmake, so this alias is a duplicate under MP_BOOT_STUBS=ON - the configuration only
// tools/boot_probe.sh uses, and the one gate.sh's duplicate count cannot see. Same rule as
// `CAudioStateWinCtor.cpp`: "Delete that alias."

// fn_58_A0
extern "C" void reachstub_266() asm("fn_58_A0");
extern "C" void reachstub_266() { mpReachStub("fn_58_A0", "fn_58_A0"); }

// fn_60_6FF0
extern "C" void reachstub_267() asm("fn_60_6FF0");
extern "C" void reachstub_267() { mpReachStub("fn_60_6FF0", "fn_60_6FF0"); }

// fn_60_7D20
extern "C" void reachstub_268() asm("fn_60_7D20");
extern "C" void reachstub_268() { mpReachStub("fn_60_7D20", "fn_60_7D20"); }

// fn_60_8E90
extern "C" void reachstub_269() asm("fn_60_8E90");
extern "C" void reachstub_269() { mpReachStub("fn_60_8E90", "fn_60_8E90"); }

// fn_60_A30
extern "C" void reachstub_270() asm("fn_60_A30");
extern "C" void reachstub_270() { mpReachStub("fn_60_A30", "fn_60_A30"); }

// fn_60_B8
extern "C" void reachstub_271() asm("fn_60_B8");
extern "C" void reachstub_271() { mpReachStub("fn_60_B8", "fn_60_B8"); }

// fn_61_70
extern "C" void reachstub_272() asm("fn_61_70");
extern "C" void reachstub_272() { mpReachStub("fn_61_70", "fn_61_70"); }

// fn_62_188
extern "C" void reachstub_273() asm("fn_62_188");
extern "C" void reachstub_273() { mpReachStub("fn_62_188", "fn_62_188"); }

// fn_65_FC
extern "C" void reachstub_274() asm("fn_65_FC");
extern "C" void reachstub_274() { mpReachStub("fn_65_FC", "fn_65_FC"); }

// fn_66_70
extern "C" void reachstub_275() asm("fn_66_70");
extern "C" void reachstub_275() { mpReachStub("fn_66_70", "fn_66_70"); }

// fn_80020478
extern "C" void reachstub_276() asm("fn_80020478");
extern "C" void reachstub_276() { mpReachStub("fn_80020478", "fn_80020478"); }

// fn_800214A0
extern "C" void reachstub_277() asm("fn_800214A0");
extern "C" void reachstub_277() { mpReachStub("fn_800214A0", "fn_800214A0"); }

// fn_80022C74
extern "C" void reachstub_278() asm("fn_80022C74");
extern "C" void reachstub_278() { mpReachStub("fn_80022C74", "fn_80022C74"); }

// fn_80038624
extern "C" void reachstub_279() asm("fn_80038624");
extern "C" void reachstub_279() { mpReachStub("fn_80038624", "fn_80038624"); }

// fn_80041518(queryOutput&, MapWorldInfoAreas&, unsigned short)
extern "C" void reachstub_280() asm("_Z11fn_80041518R11queryOutputR17MapWorldInfoAreast");
extern "C" void reachstub_280() { mpReachStub("_Z11fn_80041518R11queryOutputR17MapWorldInfoAreast", "fn_80041518(queryOutput&, MapWorldInfoAreas&, unsigned short)"); }

// fn_80049ED8(CActor*, CStateManager&)
extern "C" void reachstub_283() asm("_Z11fn_80049ED8P6CActorR13CStateManager");
extern "C" void reachstub_283() { mpReachStub("_Z11fn_80049ED8P6CActorR13CStateManager", "fn_80049ED8(CActor*, CStateManager&)"); }

// fn_8004F770
extern "C" void reachstub_284() asm("fn_8004F770");
extern "C" void reachstub_284() { mpReachStub("fn_8004F770", "fn_8004F770"); }

// fn_800CB764
extern "C" void reachstub_285() asm("fn_800CB764");
extern "C" void reachstub_285() { mpReachStub("fn_800CB764", "fn_800CB764"); }

// fn_801423A8
extern "C" void reachstub_286() asm("fn_801423A8");
extern "C" void reachstub_286() { mpReachStub("fn_801423A8", "fn_801423A8"); }

// fn_8014306C
extern "C" void reachstub_287() asm("fn_8014306C");
extern "C" void reachstub_287() { mpReachStub("fn_8014306C", "fn_8014306C"); }

// fn_801437DC
extern "C" void reachstub_288() asm("fn_801437DC");
extern "C" void reachstub_288() { mpReachStub("fn_801437DC", "fn_801437DC"); }

// fn_80143884
extern "C" void reachstub_289() asm("fn_80143884");
extern "C" void reachstub_289() { mpReachStub("fn_80143884", "fn_80143884"); }

// fn_80143E88
extern "C" void reachstub_290() asm("fn_80143E88");
extern "C" void reachstub_290() { mpReachStub("fn_80143E88", "fn_80143E88"); }

// fn_80145628
extern "C" void reachstub_291() asm("fn_80145628");
extern "C" void reachstub_291() { mpReachStub("fn_80145628", "fn_80145628"); }

// fn_80145A2C
extern "C" void reachstub_292() asm("fn_80145A2C");
extern "C" void reachstub_292() { mpReachStub("fn_80145A2C", "fn_80145A2C"); }

// fn_80180430
extern "C" void reachstub_294() asm("fn_80180430");
extern "C" void reachstub_294() { mpReachStub("fn_80180430", "fn_80180430"); }

// fn_80180598
extern "C" void reachstub_295() asm("fn_80180598");
extern "C" void reachstub_295() { mpReachStub("fn_80180598", "fn_80180598"); }

// fn_80193E08 - `src/MetroidPrime/Carve80193E08.c` is a `Matching` decomp unit for it, but it is
// deliberately NOT in `files.cmake`: its whole body is two `lis`/`addi` pairs against retail's
// own `.data` vtables (0x803B0D68 and 0x803B5CB0), so the byte-exact version writes to two
// addresses that do not exist in the host process.  Measured: with the carve in the port build
// `tools/boot_probe.sh` gets *further* than with the stub in some places and then dies on
// signal 11 inside `fn_80193E08` itself, one step earlier than the stub's fault.  The stub is
// what the port needs; see the file's header for the full measurement.
extern "C" void reachstub_297() asm("fn_80193E08");
extern "C" void reachstub_297() { mpReachStub("fn_80193E08", "fn_80193E08"); }

// fn_801C5990
extern "C" void reachstub_298() asm("fn_801C5990");
extern "C" void reachstub_298() { mpReachStub("fn_801C5990", "fn_801C5990"); }

// fn_801CA0F8__10CPlayerGunFv
extern "C" void reachstub_299() asm("fn_801CA0F8__10CPlayerGunFv");
extern "C" void reachstub_299() { mpReachStub("fn_801CA0F8__10CPlayerGunFv", "fn_801CA0F8__10CPlayerGunFv"); }

// fn_801D9F90
extern "C" void reachstub_300() asm("fn_801D9F90");
extern "C" void reachstub_300() { mpReachStub("fn_801D9F90", "fn_801D9F90"); }

// RenderEmitters__12CEchoEmitterFRC13CStateManagerPC12CEchoEmitter
extern "C" void reachstub_301() asm("RenderEmitters__12CEchoEmitterFRC13CStateManagerPC12CEchoEmitter");
extern "C" void reachstub_301() { mpReachStub("RenderEmitters__12CEchoEmitterFRC13CStateManagerPC12CEchoEmitter", "RenderEmitters__12CEchoEmitterFRC13CStateManagerPC12CEchoEmitter"); }

// fn_801F47F4
extern "C" void reachstub_302() asm("fn_801F47F4");
extern "C" void reachstub_302() { mpReachStub("fn_801F47F4", "fn_801F47F4"); }

// fn_8029AF00
extern "C" void reachstub_303() asm("fn_8029AF00");
extern "C" void reachstub_303() { mpReachStub("fn_8029AF00", "fn_8029AF00"); }

// fn_802BBDB8
extern "C" void reachstub_304() asm("fn_802BBDB8");
extern "C" void reachstub_304() { mpReachStub("fn_802BBDB8", "fn_802BBDB8"); }

// fn_802CB608
extern "C" void reachstub_305() asm("fn_802CB608");
extern "C" void reachstub_305() { mpReachStub("fn_802CB608", "fn_802CB608"); }

// fn_802FA1BC
extern "C" void reachstub_306() asm("fn_802FA1BC");
extern "C" void reachstub_306() { mpReachStub("fn_802FA1BC", "fn_802FA1BC"); }

// fn_802FA7D4
extern "C" void reachstub_307() asm("fn_802FA7D4");
extern "C" void reachstub_307() { mpReachStub("fn_802FA7D4", "fn_802FA7D4"); }

// fn_802FAAE4
extern "C" void reachstub_308() asm("fn_802FAAE4");
extern "C" void reachstub_308() { mpReachStub("fn_802FAAE4", "fn_802FAAE4"); }

// fn_80310F38
extern "C" void reachstub_309() asm("fn_80310F38");
extern "C" void reachstub_309() { mpReachStub("fn_80310F38", "fn_80310F38"); }

// fn_803111A4
extern "C" void reachstub_310() asm("fn_803111A4");
extern "C" void reachstub_310() { mpReachStub("fn_803111A4", "fn_803111A4"); }

// fn_803115F8
extern "C" void reachstub_311() asm("fn_803115F8");
extern "C" void reachstub_311() { mpReachStub("fn_803115F8", "fn_803115F8"); }

// fn_8033CDA0
extern "C" void reachstub_312() asm("fn_8033CDA0");
extern "C" void reachstub_312() { mpReachStub("fn_8033CDA0", "fn_8033CDA0"); }

// fn_8033CEE8
extern "C" void reachstub_313() asm("fn_8033CEE8");
extern "C" void reachstub_313() { mpReachStub("fn_8033CEE8", "fn_8033CEE8"); }

// lbl_70_rodata_C
extern "C" void reachstub_314() asm("lbl_70_rodata_C");
extern "C" void reachstub_314() { mpReachStub("lbl_70_rodata_C", "lbl_70_rodata_C"); }

// mp_cswarmbasics
extern "C" void reachstub_316() asm("mp_cswarmbasics");
extern "C" void reachstub_316() { mpReachStub("mp_cswarmbasics", "mp_cswarmbasics"); }

// mp_cswarmbasics_exit
extern "C" void reachstub_317() asm("mp_cswarmbasics_exit");
extern "C" void reachstub_317() { mpReachStub("mp_cswarmbasics_exit", "mp_cswarmbasics_exit"); }

// CAnimData::~CAnimData() - HAND-ADDED 2026-09-28, not in the generator's input.
//
// `src/MetroidPrime/CModelDataDtor.cpp` destroys `xc_animData`, so both the port's link and this
// build now ask for `_ZN9CAnimDataD1Ev` (retail `fn_8002C340`, 0x8002C340, 0x2F8 = 760 bytes) and
// nothing defines it. `tools/boot_probe.sh` cannot auto-stub it: ld prints the *demangled* name,
// `CAnimData::~CAnimData()` is not a C identifier, so its pass reports "left for a human" and the
// relink still fails - and a link that does not finish is the one failure the probe cannot report
// a symbol from. The real body is queued (NEW: port-animdata-dtor in
// build/goal/notes/port-modeldata-dtor.md); until it lands this logs its own name and returns,
// --- appended by tools/boot_probe.sh on 2026-09-26T20:59:09+02:00 ---
// Unresolved symbols THIS link asked for. Diagnostic only; see the file header.
extern "C" void fn_802BEC6C(void) { printf("[auto-stub] fn_802BEC6C\n"); }
extern "C" void fn_802C1FE4(void) { printf("[auto-stub] fn_802C1FE4\n"); }

// --- appended by tools/boot_probe.sh on 2026-09-26T22:03:29+02:00 ---
// Unresolved symbols THIS link asked for. Diagnostic only; see the file header.
// `lbl_803A56C0` was stubbed in this block on 2026-09-26 as a *function*, which made every
// `lbl_803A56C0 + N` in the tree read N bytes past a zero-filled body. It is a real .rodata
// object now - the 0x1C0-byte retail string pool, in
// src/MetroidPrime/PortPoolStandIns.cpp - so that one alias is deleted, which is the fix
// boot_probe.sh's own duplicate-definition branch prescribes.
extern "C" void lbl_803B5910(void) { printf("[auto-stub] lbl_803B5910\n"); }
extern "C" void lbl_8041A3C0(void) { printf("[auto-stub] lbl_8041A3C0\n"); }

// --- appended by tools/boot_probe.sh on 2026-09-26T22:59:39+02:00 ---
// Unresolved symbols THIS link asked for. Diagnostic only; see the file header.
//
// DELETED 2026-09-27 (lane `render2b`), and this is the FIFTH time this class of deletion has
// had to be done by hand - see the file header. The reason is always the same and always
// invisible to `tools/gate.sh`: `tools/boot_probe.sh` builds with `-DMP_BOOT_STUBS=ON`, which
// makes the link SUCCEED and reports 0 undefined, and gate.sh's duplicate count never sees that
// configuration, so an alias here and the real definition both reach the boot link and the
// duplicate only shows up in `tools/probe_sources.sh` or the real port link.
//
// The two definitions these shadow are now in `files.cmake`:
//
//   `fn_80272958`  src/MetaRender/Carve80272958.c  - retail 0x80272958, 0x30, byte-exact, Matching.
//   `fn_80271238`  src/MetaRender/Carve80271238.cpp - retail 0x80271238, 0x59C, NonMatching 98.92%.
//                  It is the C++ member `CCubeRenderer::CCubeRenderer`, so the host symbol is
//                  `CCubeRenderer::CCubeRenderer`, NOT the `extern "C" fn_80271238` alias that
//                  src/MetaRender/Carve8026EF54.cpp declares: that alias is a different symbol and
//                  is what `mp_CCubeRenderer_ctor` (src/MetaRender/PortCCubeRenderer.cpp) exists
//                  to bridge. See the correction in src/MetaRender/Carve8026EF54.cpp.
//
// So keeping either stub is a duplicate under -DMP_BOOT_STUBS=ON. The reason is written here so
// the next collection does not re-add them.

// --- appended by tools/boot_probe.sh on 2026-09-26T23:40:25+02:00 ---
// Unresolved symbols THIS link asked for. Diagnostic only; see the file header.
extern "C" void lbl_803B5CB0(void) { printf("[auto-stub] lbl_803B5CB0\n"); }

// --- appended by tools/boot_probe.sh on 2026-09-27T01:24:59+02:00 ---
// Unresolved symbols THIS link asked for. Diagnostic only; see the file header.
// RETIRED 2026-09-27, auto-stubs `fn_80301CC4` and `lbl_80418BA8`. Both now have REAL definitions
// in src/Kyoto/CARAMManagerPort.cpp (lines 197 and 205 there), so these are duplicates under
// -DMP_BOOT_STUBS=ON - the only configuration tools/boot_probe.sh builds, and the one gate.sh's
// duplicate count cannot see. This is the SIXTH instance of this deletion in this project and the
// reason is written here every time, because it has now been rediscovered six times.
//
// Note the shape of this pair, which is why the recipe above matters: `lbl_80418BA8` is a DATA
// symbol (a `.bss` size word) and `fn_80301CC4` is the function reading it. The stub generator
// emitted both as `extern "C" void ...`, i.e. it stubbed a data symbol as a function - the third
// instance of that specific mistake, after `lbl_80418BA8` twice by `boot_probe.sh`'s self-heal.

// --- appended by tools/boot_probe.sh on 2026-09-27T11:37:07+02:00 ---
// Unresolved symbols THIS link asked for. Diagnostic only; see the file header.
extern "C" void fn_80270A64(void) { printf("[auto-stub] fn_80270A64\n"); }
extern "C" void fn_80270BB4(void) { printf("[auto-stub] fn_80270BB4\n"); }
extern "C" void fn_80270D44(void) { printf("[auto-stub] fn_80270D44\n"); }
extern "C" void fn_80270EC8(void) { printf("[auto-stub] fn_80270EC8\n"); }
extern "C" void fn_80271104(void) { printf("[auto-stub] fn_80271104\n"); }
extern "C" void fn_802711A4(void) { printf("[auto-stub] fn_802711A4\n"); }
extern "C" void fn_80272624(void) { printf("[auto-stub] fn_80272624\n"); }
extern "C" void fn_802C420C(void) { printf("[auto-stub] fn_802C420C\n"); }

// --- regenerated 2026-09-29 from a `-Wl,--no-demangle` link of the boot probe ---
// 109 aliases were deleted above because the tree now defines them for real (they
// were duplicates under -DMP_BOOT_STUBS=ON). These are the symbols that same link still
// named as undefined. Data symbols (no parameter list when demangled, `lbl_`, vtables) get
// zeroed storage rather than a function body, so a read sees 0 instead of code bytes.
// fn_80041518(queryOutput&, rstl::bit_vector<rstl::rmemory_allocator>&, unsigned short)
extern "C" void reachstub_318() asm("_Z11fn_80041518R11queryOutputRN4rstl10bit_vectorINS1_17rmemory_allocatorEEEt");
extern "C" void reachstub_318() { mpReachStub("_Z11fn_80041518R11queryOutputRN4rstl10bit_vectorINS1_17rmemory_allocatorEEEt", "fn_80041518(queryOutput&, rstl::bit_vector<rstl::rmemory_allocator>&, unsigned short)"); }

// LoadAreaAttributes(CStateManager&, CInputStream&, CEntityInfo const&)
extern "C" void reachstub_320() asm("_Z18LoadAreaAttributesR13CStateManagerR12CInputStreamRK11CEntityInfo");
extern "C" void reachstub_320() { mpReachStub("_Z18LoadAreaAttributesR13CStateManagerR12CInputStreamRK11CEntityInfo", "LoadAreaAttributes(CStateManager&, CInputStream&, CEntityInfo const&)"); }

// FindMinMaxConnectionTimes(rstl::vector<SLdrConnection, rstl::rmemory_allocator> const&)
extern "C" void reachstub_322() asm("_Z25FindMinMaxConnectionTimesRKN4rstl6vectorI14SLdrConnectionNS_17rmemory_allocatorEEE");
extern "C" void reachstub_322() { mpReachStub("_Z25FindMinMaxConnectionTimesRKN4rstl6vectorI14SLdrConnectionNS_17rmemory_allocatorEEE", "FindMinMaxConnectionTimes(rstl::vector<SLdrConnection, rstl::rmemory_allocator> const&)"); }

// LoadTypedefDamageInfo(SLdrDamageInfo&, CInputStream&)
extern "C" void reachstub_323() asm("_Z25LoadTypedefSLdrDamageInfoR14SLdrDamageInfoR12CInputStream");
extern "C" void reachstub_323() { mpReachStub("_Z25LoadTypedefSLdrDamageInfoR14SLdrDamageInfoR12CInputStream", "LoadTypedefDamageInfo(SLdrDamageInfo&, CInputStream&)"); }

// CAuxWeapon::fn_801D5DD0(int, CStateManager&)
extern "C" void reachstub_325() asm("_ZN10CAuxWeapon11fn_801D5DD0EiR13CStateManager");
extern "C" void reachstub_325() { mpReachStub("_ZN10CAuxWeapon11fn_801D5DD0EiR13CStateManager", "CAuxWeapon::fn_801D5DD0(int, CStateManager&)"); }

// CAuxWeapon::fn_801D6894(CStateManager&, bool)
extern "C" void reachstub_326() asm("_ZN10CAuxWeapon11fn_801D6894ER13CStateManagerb");
extern "C" void reachstub_326() { mpReachStub("_ZN10CAuxWeapon11fn_801D6894ER13CStateManagerb", "CAuxWeapon::fn_801D6894(CStateManager&, bool)"); }

// CAuxWeapon::~CAuxWeapon()
extern "C" void reachstub_328() asm("_ZN10CAuxWeaponD1Ev");
extern "C" void reachstub_328() { mpReachStub("_ZN10CAuxWeaponD1Ev", "CAuxWeapon::~CAuxWeapon()"); }

// CGameState::~CGameState()
extern "C" void reachstub_329() asm("_ZN10CGameStateD1Ev");
extern "C" void reachstub_329() { mpReachStub("_ZN10CGameStateD1Ev", "CGameState::~CGameState()"); }

// CGunMotion::fn_801D6D8C()
extern "C" void reachstub_330() asm("_ZN10CGunMotion11fn_801D6D8CEv");
extern "C" void reachstub_330() { mpReachStub("_ZN10CGunMotion11fn_801D6D8CEv", "CGunMotion::fn_801D6D8C()"); }

// CGunMotion::fn_801D6ED0(int, CStateManager&, float, bool)
extern "C" void reachstub_331() asm("_ZN10CGunMotion11fn_801D6ED0EiR13CStateManagerfb");
extern "C" void reachstub_331() { mpReachStub("_ZN10CGunMotion11fn_801D6ED0EiR13CStateManagerfb", "CGunMotion::fn_801D6ED0(int, CStateManager&, float, bool)"); }

// CGameCamera::UpdatePerspective(float, CStateManager&)
extern "C" void reachstub_334() asm("_ZN11CGameCamera17UpdatePerspectiveEfR13CStateManager");
extern "C" void reachstub_334() { mpReachStub("_ZN11CGameCamera17UpdatePerspectiveEfR13CStateManager", "CGameCamera::UpdatePerspective(float, CStateManager&)"); }

// CGrappleArm::CGrappleArm(CVector3f const&, TUniqueId, bool)
extern "C" void reachstub_335() asm("_ZN11CGrappleArmC1ERK9CVector3f9TUniqueIdb");
extern "C" void reachstub_335() { mpReachStub("_ZN11CGrappleArmC1ERK9CVector3f9TUniqueIdb", "CGrappleArm::CGrappleArm(CVector3f const&, TUniqueId, bool)"); }

// CPortalArea::UpdateActor(CStateManager&, CActor&)
extern "C" void reachstub_336() asm("_ZN11CPortalArea11UpdateActorER13CStateManagerR6CActor");
extern "C" void reachstub_336() { mpReachStub("_ZN11CPortalArea11UpdateActorER13CStateManagerR6CActor", "CPortalArea::UpdateActor(CStateManager&, CActor&)"); }

// CEchoEmitter::CEchoEmitter(CAABox const&, SEchoParameters const&)
extern "C" void reachstub_338() asm("_ZN12CEchoEmitterC1ERK6CAABoxRK15SEchoParameters");
extern "C" void reachstub_338() { mpReachStub("_ZN12CEchoEmitterC1ERK6CAABoxRK15SEchoParameters", "CEchoEmitter::CEchoEmitter(CAABox const&, SEchoParameters const&)"); }

// CTweakPlayer::GetBallRadius()
extern "C" void reachstub_339() asm("_ZN12CTweakPlayer13GetBallRadiusEv");
extern "C" void reachstub_339() { mpReachStub("_ZN12CTweakPlayer13GetBallRadiusEv", "CTweakPlayer::GetBallRadius()"); }

// CWorldShadow::CWorldShadow(unsigned int, unsigned int, bool)
extern "C" void reachstub_340() asm("_ZN12CWorldShadowC1Ejjb");
extern "C" void reachstub_340() { mpReachStub("_ZN12CWorldShadowC1Ejjb", "CWorldShadow::CWorldShadow(unsigned int, unsigned int, bool)"); }

// CWorldShadow::~CWorldShadow()
extern "C" void reachstub_341() asm("_ZN12CWorldShadowD1Ev");
extern "C" void reachstub_341() { mpReachStub("_ZN12CWorldShadowD1Ev", "CWorldShadow::~CWorldShadow()"); }

// CScriptEffect::CScriptEffect(TUniqueId, rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator> const&, CEntityInfo const&, CTransform4f const&, CVector3f const&, unsigned int, bool, bool, bool, bool, float, float, float, float, bool, float, float, float, bool, bool, bool, CLightParameters const&, bool, CGameSplineDesc const&, bool, bool, bool, CScriptEffect::ERenderOrder)
extern "C" void reachstub_342() asm("_ZN13CScriptEffectC1E9TUniqueIdRKN4rstl12basic_stringIcNS1_11char_traitsIcEENS1_17rmemory_allocatorEEERK11CEntityInfoRK12CTransform4fRK9CVector3fjbbbbffffbfffbbbRK16CLightParametersbRK15CGameSplineDescbbbNS_12ERenderOrderE");
extern "C" void reachstub_342() { mpReachStub("_ZN13CScriptEffectC1E9TUniqueIdRKN4rstl12basic_stringIcNS1_11char_traitsIcEENS1_17rmemory_allocatorEEERK11CEntityInfoRK12CTransform4fRK9CVector3fjbbbbffffbfffbbbRK16CLightParametersbRK15CGameSplineDescbbbNS_12ERenderOrderE", "CScriptEffect::CScriptEffect(TUniqueId, rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator> const&, CEntityInfo const&, CTransform4f const&, CVector3f const&, unsigned int, bool, bool, bool, bool, float, float, float, float, bool, float, float, float, bool, bool, bool, CLightParameters const&, bool, CGameSplineDesc const&, bool, bool, bool, CScriptEffect::ERenderOrder)"); }

// CScriptPickup::ShowAllKeysCollectedAlert is defined for real in
// src/MetroidPrime/ScriptObjects/CScriptPickup.cpp (retail 0x800B41FC), so reachstub_343 was
// retired here by hand on 2026-10-02; the two definitions otherwise collide in the boot probe,
// which links with this file. Same hand-edit AllocateRenderer needed above.

// CSkinnedModel::CSkinnedModel(TLockedToken<CModel> const&, TLockedToken<CSkinRules> const&, TLockedToken<CCharLayoutInfo> const&)
extern "C" void reachstub_345() asm("_ZN13CSkinnedModelC1ERK12TLockedTokenI6CModelERKS0_I10CSkinRulesERKS0_I15CCharLayoutInfoE");
extern "C" void reachstub_345() { mpReachStub("_ZN13CSkinnedModelC1ERK12TLockedTokenI6CModelERKS0_I10CSkinRulesERKS0_I15CCharLayoutInfoE", "CSkinnedModel::CSkinnedModel(TLockedToken<CModel> const&, TLockedToken<CSkinRules> const&, TLockedToken<CCharLayoutInfo> const&)"); }

// CSkinnedModel::~CSkinnedModel()
extern "C" void reachstub_346() asm("_ZN13CSkinnedModelD1Ev");
extern "C" void reachstub_346() { mpReachStub("_ZN13CSkinnedModelD1Ev", "CSkinnedModel::~CSkinnedModel()"); }

// CBodyStateInfo::~CBodyStateInfo()
extern "C" void reachstub_347() asm("_ZN14CBodyStateInfoD1Ev");
extern "C" void reachstub_347() { mpReachStub("_ZN14CBodyStateInfoD1Ev", "CBodyStateInfo::~CBodyStateInfo()"); }

// CGameCollision::RayDynamicIntersection(CStateManager const&, TUniqueId&, CVector3f const&, CVector3f const&, float, CMaterialFilter const&, rstl::reserved_vector< TUniqueId, kMaxObjects > const&)
extern "C" void reachstub_348() asm("_ZN14CGameCollision22RayDynamicIntersectionERK13CStateManagerR9TUniqueIdRK9CVector3fS7_fRK15CMaterialFilterRKN4rstl15reserved_vectorIS3_Li1024EEE");
extern "C" void reachstub_348() { mpReachStub("_ZN14CGameCollision22RayDynamicIntersectionERK13CStateManagerR9TUniqueIdRK9CVector3fS7_fRK15CMaterialFilterRKN4rstl15reserved_vectorIS3_Li1024EEE", "CGameCollision::RayDynamicIntersection(CStateManager const&, TUniqueId&, CVector3f const&, CVector3f const&, float, CMaterialFilter const&, rstl::reserved_vector< TUniqueId, kMaxObjects > const&)"); }

// CAiKnockBackMgr::KnockBack(CStateManager&, CActor&, CKnockBackInfo const&)
extern "C" void reachstub_354() asm("_ZN15CAiKnockBackMgr9KnockBackER13CStateManagerR6CActorRK14CKnockBackInfo");
extern "C" void reachstub_354() { mpReachStub("_ZN15CAiKnockBackMgr9KnockBackER13CStateManagerR6CActorRK14CKnockBackInfo", "CAiKnockBackMgr::KnockBack(CStateManager&, CActor&, CKnockBackInfo const&)"); }

// CAiKnockBackMgr::CAiKnockBackMgr(unsigned int)
extern "C" void reachstub_355() asm("_ZN15CAiKnockBackMgrC1Ej");
extern "C" void reachstub_355() { mpReachStub("_ZN15CAiKnockBackMgrC1Ej", "CAiKnockBackMgr::CAiKnockBackMgr(unsigned int)"); }

// CAiKnockBackMgr::~CAiKnockBackMgr()
extern "C" void reachstub_356() asm("_ZN15CAiKnockBackMgrD1Ev");
extern "C" void reachstub_356() { mpReachStub("_ZN15CAiKnockBackMgrD1Ev", "CAiKnockBackMgr::~CAiKnockBackMgr()"); }

// CAnimationState::CAnimationState()
extern "C" void reachstub_357() asm("_ZN15CAnimationStateC1Ev");
extern "C" void reachstub_357() { mpReachStub("_ZN15CAnimationStateC1Ev", "CAnimationState::CAnimationState()"); }

// CGameSplineDesc::CGameSplineDesc(SLdrSpline const&, CMotionSpline::ESplineType, float, bool)
extern "C" void reachstub_358() asm("_ZN15CGameSplineDescC1ERK10SLdrSplineN13CMotionSpline11ESplineTypeEfb");
extern "C" void reachstub_358() { mpReachStub("_ZN15CGameSplineDescC1ERK10SLdrSplineN13CMotionSpline11ESplineTypeEfb", "CGameSplineDesc::CGameSplineDesc(SLdrSpline const&, CMotionSpline::ESplineType, float, bool)"); }

// CMappableObject::ReadAutomapperTweaks()
extern "C" void reachstub_359() asm("_ZN15CMappableObject20ReadAutomapperTweaksEv");
extern "C" void reachstub_359() { mpReachStub("_ZN15CMappableObject20ReadAutomapperTweaksEv", "CMappableObject::ReadAutomapperTweaks()"); }

// CParticleSwoosh::CParticleSwoosh(TToken<CSwooshDescription>, int)
extern "C" void reachstub_360() asm("_ZN15CParticleSwooshC1E6TTokenI18CSwooshDescriptionEi");
extern "C" void reachstub_360() { mpReachStub("_ZN15CParticleSwooshC1E6TTokenI18CSwooshDescriptionEi", "CParticleSwoosh::CParticleSwoosh(TToken<CSwooshDescription>, int)"); }

// CSaveGameScreen::CSaveGameScreen(ESaveContext, unsigned long)
extern "C" void reachstub_361() asm("_ZN15CSaveGameScreenC1E12ESaveContextm");
extern "C" void reachstub_361() { mpReachStub("_ZN15CSaveGameScreenC1E12ESaveContextm", "CSaveGameScreen::CSaveGameScreen(ESaveContext, unsigned long)"); }

// CTweakPlayerGun::InitBeamInfo()
extern "C" void reachstub_362() asm("_ZN15CTweakPlayerGun12InitBeamInfoEv");
extern "C" void reachstub_362() { mpReachStub("_ZN15CTweakPlayerGun12InitBeamInfoEv", "CTweakPlayerGun::InitBeamInfo()"); }

// CTweakPlayerRes::ResolveResources()
extern "C" void reachstub_363() asm("_ZN15CTweakPlayerRes16ResolveResourcesEv");
extern "C" void reachstub_363() { mpReachStub("_ZN15CTweakPlayerRes16ResolveResourcesEv", "CTweakPlayerRes::ResolveResources()"); }

// CPASAnimParmData::CPASAnimParmData(pas::EAnimationState, CPASAnimParm const&, CPASAnimParm const&, CPASAnimParm const&, CPASAnimParm const&, CPASAnimParm const&, CPASAnimParm const&, CPASAnimParm const&, CPASAnimParm const&)
extern "C" void reachstub_364() asm("_ZN16CPASAnimParmDataC1EN3pas15EAnimationStateERK12CPASAnimParmS4_S4_S4_S4_S4_S4_S4_");
extern "C" void reachstub_364() { mpReachStub("_ZN16CPASAnimParmDataC1EN3pas15EAnimationStateERK12CPASAnimParmS4_S4_S4_S4_S4_S4_S4_", "CPASAnimParmData::CPASAnimParmData(pas::EAnimationState, CPASAnimParm const&, CPASAnimParm const&, CPASAnimParm const&, CPASAnimParm const&, CPASAnimParm const&, CPASAnimParm const&, CPASAnimParm const&, CPASAnimParm const&)"); }

// CPlayerCameraBob::ReadTweaks(SLdrTweakCameraBob const&)
extern "C" void reachstub_365() asm("_ZN16CPlayerCameraBob10ReadTweaksERK18SLdrTweakCameraBob");
extern "C" void reachstub_365() { mpReachStub("_ZN16CPlayerCameraBob10ReadTweaksERK18SLdrTweakCameraBob", "CPlayerCameraBob::ReadTweaks(SLdrTweakCameraBob const&)"); }

// CAnimationManager::~CAnimationManager()
extern "C" void reachstub_366() asm("_ZN17CAnimationManagerD1Ev");
extern "C" void reachstub_366() { mpReachStub("_ZN17CAnimationManagerD1Ev", "CAnimationManager::~CAnimationManager()"); }

// CDSPStreamManager::UpdateVolume(int, int)
extern "C" void reachstub_367() asm("_ZN17CDSPStreamManager12UpdateVolumeEii");
extern "C" void reachstub_367() { mpReachStub("_ZN17CDSPStreamManager12UpdateVolumeEii", "CDSPStreamManager::UpdateVolume(int, int)"); }

// CDSPStreamManager::StopStreaming(int)
extern "C" void reachstub_368() asm("_ZN17CDSPStreamManager13StopStreamingEi");
extern "C" void reachstub_368() { mpReachStub("_ZN17CDSPStreamManager13StopStreamingEi", "CDSPStreamManager::StopStreaming(int)"); }

// CDSPStreamManager::GetStreamState(int)
extern "C" void reachstub_369() asm("_ZN17CDSPStreamManager14GetStreamStateEi");
extern "C" void reachstub_369() { mpReachStub("_ZN17CDSPStreamManager14GetStreamStateEi", "CDSPStreamManager::GetStreamState(int)"); }

// CDSPStreamManager::StartStreaming(rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator> const&, int, int)
extern "C" void reachstub_370() asm("_ZN17CDSPStreamManager14StartStreamingERKN4rstl12basic_stringIcNS0_11char_traitsIcEENS0_17rmemory_allocatorEEEii");
extern "C" void reachstub_370() { mpReachStub("_ZN17CDSPStreamManager14StartStreamingERKN4rstl12basic_stringIcNS0_11char_traitsIcEENS0_17rmemory_allocatorEEEii", "CDSPStreamManager::StartStreaming(rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator> const&, int, int)"); }

// CDSPStreamManager::IsStreamAvailable(int)
extern "C" void reachstub_371() asm("_ZN17CDSPStreamManager17IsStreamAvailableEi");
extern "C" void reachstub_371() { mpReachStub("_ZN17CDSPStreamManager17IsStreamAvailableEi", "CDSPStreamManager::IsStreamAvailable(int)"); }

// CDSPStreamManager::CanStop(int)
extern "C" void reachstub_372() asm("_ZN17CDSPStreamManager7CanStopEi");
extern "C" void reachstub_372() { mpReachStub("_ZN17CDSPStreamManager7CanStopEi", "CDSPStreamManager::CanStop(int)"); }

// CParticleElectric::CParticleElectric(TToken<CElectricDescription>)
extern "C" void reachstub_373() asm("_ZN17CParticleElectricC1E6TTokenI20CElectricDescriptionE");
extern "C" void reachstub_373() { mpReachStub("_ZN17CParticleElectricC1E6TTokenI20CElectricDescriptionE", "CParticleElectric::CParticleElectric(TToken<CElectricDescription>)"); }

// CPortalTransition::~CPortalTransition()
extern "C" void reachstub_374() asm("_ZN17CPortalTransitionD1Ev");
extern "C" void reachstub_374() { mpReachStub("_ZN17CPortalTransitionD1Ev", "CPortalTransition::~CPortalTransition()"); }

// CPersistentOptions::FindEnvironmentVariable(char const*)
extern "C" void reachstub_377() asm("_ZN18CPersistentOptions23FindEnvironmentVariableEPKc");
extern "C" void reachstub_377() { mpReachStub("_ZN18CPersistentOptions23FindEnvironmentVariableEPKc", "CPersistentOptions::FindEnvironmentVariable(char const*)"); }

// CTransitionManager::~CTransitionManager()
extern "C" void reachstub_378() asm("_ZN18CTransitionManagerD1Ev");
extern "C" void reachstub_378() { mpReachStub("_ZN18CTransitionManagerD1Ev", "CTransitionManager::~CTransitionManager()"); }

// CPathFindNavigation::CPathFindNavigation()
extern "C" void reachstub_379() asm("_ZN19CPathFindNavigationC1Ev");
extern "C" void reachstub_379() { mpReachStub("_ZN19CPathFindNavigationC1Ev", "CPathFindNavigation::CPathFindNavigation()"); }

// CPlayerKnockBackMgr::CPlayerKnockBackMgr()
extern "C" void reachstub_380() asm("_ZN19CPlayerKnockBackMgrC1Ev");
extern "C" void reachstub_380() { mpReachStub("_ZN19CPlayerKnockBackMgrC1Ev", "CPlayerKnockBackMgr::CPlayerKnockBackMgr()"); }

// CPlayerKnockBackMgr::~CPlayerKnockBackMgr()
extern "C" void reachstub_381() asm("_ZN19CPlayerKnockBackMgrD1Ev");
extern "C" void reachstub_381() { mpReachStub("_ZN19CPlayerKnockBackMgrD1Ev", "CPlayerKnockBackMgr::~CPlayerKnockBackMgr()"); }

// CWaypointNavigation::Patrol(CStateManager&, EStateMsg, float, CPatterned&)
extern "C" void reachstub_382() asm("_ZN19CWaypointNavigation6PatrolER13CStateManager9EStateMsgfR10CPatterned");
extern "C" void reachstub_382() { mpReachStub("_ZN19CWaypointNavigation6PatrolER13CStateManager9EStateMsgfR10CPatterned", "CWaypointNavigation::Patrol(CStateManager&, EStateMsg, float, CPatterned&)"); }

// CWaypointNavigation::CWaypointNavigation()
extern "C" void reachstub_383() asm("_ZN19CWaypointNavigationC1Ev");
extern "C" void reachstub_383() { mpReachStub("_ZN19CWaypointNavigationC1Ev", "CWaypointNavigation::CWaypointNavigation()"); }

// TStateMachineState2<CPatterned>::Setup(CStateMachine2 const&)
extern "C" void reachstub_384() asm("_ZN19TStateMachineState2I10CPatternedE5SetupERK14CStateMachine2");
extern "C" void reachstub_384() { mpReachStub("_ZN19TStateMachineState2I10CPatternedE5SetupERK14CStateMachine2", "TStateMachineState2<CPatterned>::Setup(CStateMachine2 const&)"); }

// TStateMachineState2<CPatterned>::TStateMachineState2()
extern "C" void reachstub_385() asm("_ZN19TStateMachineState2I10CPatternedEC1Ev");
extern "C" void reachstub_385() { mpReachStub("_ZN19TStateMachineState2I10CPatternedEC1Ev", "TStateMachineState2<CPatterned>::TStateMachineState2()"); }

// CDamageVulnerability::CDamageVulnerability(SLdrDamageVulnerability const&)
extern "C" void reachstub_387() asm("_ZN20CDamageVulnerabilityC1ERK23SLdrDamageVulnerability");
extern "C" void reachstub_387() { mpReachStub("_ZN20CDamageVulnerabilityC1ERK23SLdrDamageVulnerability", "CDamageVulnerability::CDamageVulnerability(SLdrDamageVulnerability const&)"); }

// CInterpolationCamera::SetInterpolation(CTransform4f const&, TUniqueId, TUniqueId, bool, CInterpolationCamera::EPositionMode, CInterpolationCamera::ERotationMode, CStateManager&, bool, float, float)
extern "C" void reachstub_389() asm("_ZN20CInterpolationCamera16SetInterpolationERK12CTransform4f9TUniqueIdS3_bNS_13EPositionModeENS_13ERotationModeER13CStateManagerbff");
extern "C" void reachstub_389() { mpReachStub("_ZN20CInterpolationCamera16SetInterpolationERK12CTransform4f9TUniqueIdS3_bNS_13EPositionModeENS_13ERotationModeER13CStateManagerbff", "CInterpolationCamera::SetInterpolation(CTransform4f const&, TUniqueId, TUniqueId, bool, CInterpolationCamera::EPositionMode, CInterpolationCamera::ERotationMode, CStateManager&, bool, float, float)"); }

// CParticleSpawnRandom::CParticleSpawnRandom(TToken<CSpawnRandomDescription>, CElementGen::EOptionalSystemFlags, bool)
extern "C" void reachstub_390() asm("_ZN20CParticleSpawnRandomC1E6TTokenI23CSpawnRandomDescriptionEN11CElementGen20EOptionalSystemFlagsEb");
extern "C" void reachstub_390() { mpReachStub("_ZN20CParticleSpawnRandomC1E6TTokenI23CSpawnRandomDescriptionEN11CElementGen20EOptionalSystemFlagsEb", "CParticleSpawnRandom::CParticleSpawnRandom(TToken<CSpawnRandomDescription>, CElementGen::EOptionalSystemFlags, bool)"); }

// CParticleSpawnSystem::CParticleSpawnSystem(TToken<CSpawnSystemDescription>, CElementGen::EOptionalSystemFlags, bool)
extern "C" void reachstub_391() asm("_ZN20CParticleSpawnSystemC1E6TTokenI23CSpawnSystemDescriptionEN11CElementGen20EOptionalSystemFlagsEb");
extern "C" void reachstub_391() { mpReachStub("_ZN20CParticleSpawnSystemC1E6TTokenI23CSpawnSystemDescriptionEN11CElementGen20EOptionalSystemFlagsEb", "CParticleSpawnSystem::CParticleSpawnSystem(TToken<CSpawnSystemDescription>, CElementGen::EOptionalSystemFlags, bool)"); }

// CCompoundTargetReticle::~CCompoundTargetReticle()
extern "C" void reachstub_392() asm("_ZN22CCompoundTargetReticleD1Ev");
extern "C" void reachstub_392() { mpReachStub("_ZN22CCompoundTargetReticleD1Ev", "CCompoundTargetReticle::~CCompoundTargetReticle()"); }

// CJointData_LinearStorage::ResetScales()
extern "C" void reachstub_393() asm("_ZN24CJointData_LinearStorage11ResetScalesEv");
extern "C" void reachstub_393() { mpReachStub("_ZN24CJointData_LinearStorage11ResetScalesEv", "CJointData_LinearStorage::ResetScales()"); }

// CJointData_LinearStorage::SetZeroRotation()
extern "C" void reachstub_394() asm("_ZN24CJointData_LinearStorage15SetZeroRotationEv");
extern "C" void reachstub_394() { mpReachStub("_ZN24CJointData_LinearStorage15SetZeroRotationEv", "CJointData_LinearStorage::SetZeroRotation()"); }

// CJointData_LinearStorage::SetReferenceOffsets(CCharLayoutInfo const&)
extern "C" void reachstub_395() asm("_ZN24CJointData_LinearStorage19SetReferenceOffsetsERK15CCharLayoutInfo");
extern "C" void reachstub_395() { mpReachStub("_ZN24CJointData_LinearStorage19SetReferenceOffsetsERK15CCharLayoutInfo", "CJointData_LinearStorage::SetReferenceOffsets(CCharLayoutInfo const&)"); }

// CJointData_LinearStorage::CJointData_LinearStorage(int, CJointData_LinearStorage::EAllocateFrom)
extern "C" void reachstub_396() asm("_ZN24CJointData_LinearStorageC1EiNS_13EAllocateFromE");
extern "C" void reachstub_396() { mpReachStub("_ZN24CJointData_LinearStorageC1EiNS_13EAllocateFromE", "CJointData_LinearStorage::CJointData_LinearStorage(int, CJointData_LinearStorage::EAllocateFrom)"); }

// CJointData_LinearStorage::~CJointData_LinearStorage()
extern "C" void reachstub_397() asm("_ZN24CJointData_LinearStorageD1Ev");
extern "C" void reachstub_397() { mpReachStub("_ZN24CJointData_LinearStorageD1Ev", "CJointData_LinearStorage::~CJointData_LinearStorage()"); }

// CModel::SShader::~SShader()
extern "C" void reachstub_398() asm("_ZN6CModel7SShaderD1Ev");
extern "C" void reachstub_398() { mpReachStub("_ZN6CModel7SShaderD1Ev", "CModel::SShader::~SShader()"); }

// CPlayer::PlaySfxForPlayer(unsigned int, short, TAreaId, bool, int)
extern "C" void reachstub_399() asm("_ZN7CPlayer16PlaySfxForPlayerEjs7TAreaIdbi");
extern "C" void reachstub_399() { mpReachStub("_ZN7CPlayer16PlaySfxForPlayerEjs7TAreaIdbi", "CPlayer::PlaySfxForPlayer(unsigned int, short, TAreaId, bool, int)"); }

// CAnimRes::kDefaultCharIdx
extern "C" __attribute__((aligned(32))) char reachdata_400[0x400] asm("_ZN8CAnimRes15kDefaultCharIdxE");
__attribute__((aligned(32))) char reachdata_400[0x400] = {};

// CTexture::ScheduleDeletion()
extern "C" void reachstub_401() asm("_ZN8CTexture16ScheduleDeletionEv");
extern "C" void reachstub_401() { mpReachStub("_ZN8CTexture16ScheduleDeletionEv", "CTexture::ScheduleDeletion()"); }

// CAudioSys::SfxSetFilter(unsigned int, unsigned int, unsigned int)
extern "C" void reachstub_402() asm("_ZN9CAudioSys12SfxSetFilterEjjj");
extern "C" void reachstub_402() { mpReachStub("_ZN9CAudioSys12SfxSetFilterEjjj", "CAudioSys::SfxSetFilter(unsigned int, unsigned int, unsigned int)"); }

// CAudioSys::TrkQueueTrack(rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator> const&, void (*)(unsigned int), unsigned int)
extern "C" void reachstub_403() asm("_ZN9CAudioSys13TrkQueueTrackERKN4rstl12basic_stringIcNS0_11char_traitsIcEENS0_17rmemory_allocatorEEEPFvjEj");
extern "C" void reachstub_403() { mpReachStub("_ZN9CAudioSys13TrkQueueTrackERKN4rstl12basic_stringIcNS0_11char_traitsIcEENS0_17rmemory_allocatorEEEPFvjEj", "CAudioSys::TrkQueueTrack(rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator> const&, void (*)(unsigned int), unsigned int)"); }

// CAudioSys::S3dAddListener(CVector3f const&, CVector3f const&, CVector3f const&, CVector3f const&, float, float, float, unsigned int, unsigned char)
extern "C" void reachstub_404() asm("_ZN9CAudioSys14S3dAddListenerERK9CVector3fS2_S2_S2_fffjh");
extern "C" void reachstub_404() { mpReachStub("_ZN9CAudioSys14S3dAddListenerERK9CVector3fS2_S2_S2_fffjh", "CAudioSys::S3dAddListener(CVector3f const&, CVector3f const&, CVector3f const&, CVector3f const&, float, float, float, unsigned int, unsigned char)"); }

// CAudioSys::TrkFlushTracks()
extern "C" void reachstub_405() asm("_ZN9CAudioSys14TrkFlushTracksEv");
extern "C" void reachstub_405() { mpReachStub("_ZN9CAudioSys14TrkFlushTracksEv", "CAudioSys::TrkFlushTracks()"); }

// CAudioSys::S3dCheckEmitter(unsigned int)
extern "C" void reachstub_406() asm("_ZN9CAudioSys15S3dCheckEmitterEj");
extern "C" void reachstub_406() { mpReachStub("_ZN9CAudioSys15S3dCheckEmitterEj", "CAudioSys::S3dCheckEmitter(unsigned int)"); }

// CAudioSys::S3dRemoveEmitter(unsigned int)
extern "C" void reachstub_407() asm("_ZN9CAudioSys16S3dRemoveEmitterEj");
extern "C" void reachstub_407() { mpReachStub("_ZN9CAudioSys16S3dRemoveEmitterEj", "CAudioSys::S3dRemoveEmitter(unsigned int)"); }

// CAudioSys::S3dUpdateEmitter(unsigned int, CVector3f const&, CVector3f const&, unsigned char)
extern "C" void reachstub_408() asm("_ZN9CAudioSys16S3dUpdateEmitterEjRK9CVector3fS2_h");
extern "C" void reachstub_408() { mpReachStub("_ZN9CAudioSys16S3dUpdateEmitterEjRK9CVector3fS2_h", "CAudioSys::S3dUpdateEmitter(unsigned int, CVector3f const&, CVector3f const&, unsigned char)"); }

// CAudioSys::S3dEmitterVoiceID(unsigned int)
extern "C" void reachstub_409() asm("_ZN9CAudioSys17S3dEmitterVoiceIDEj");
extern "C" void reachstub_409() { mpReachStub("_ZN9CAudioSys17S3dEmitterVoiceIDEj", "CAudioSys::S3dEmitterVoiceID(unsigned int)"); }

// CAudioSys::C3DEmitterParmData::C3DEmitterParmData(float, float, unsigned int, unsigned char, unsigned char)
extern "C" void reachstub_410() asm("_ZN9CAudioSys18C3DEmitterParmDataC1Effjhh");
extern "C" void reachstub_410() { mpReachStub("_ZN9CAudioSys18C3DEmitterParmDataC1Effjhh", "CAudioSys::C3DEmitterParmData::C3DEmitterParmData(float, float, unsigned int, unsigned char, unsigned char)"); }

// CAudioSys::S3dAddEmitterParaEx(CAudioSys::C3DEmitterParmData const&, unsigned short, SND_PARAMETER_INFO*)
extern "C" void reachstub_411() asm("_ZN9CAudioSys19S3dAddEmitterParaExERKNS_18C3DEmitterParmDataEtP18SND_PARAMETER_INFO");
extern "C" void reachstub_411() { mpReachStub("_ZN9CAudioSys19S3dAddEmitterParaExERKNS_18C3DEmitterParmDataEtP18SND_PARAMETER_INFO", "CAudioSys::S3dAddEmitterParaEx(CAudioSys::C3DEmitterParmData const&, unsigned short, SND_PARAMETER_INFO*)"); }

// CAudioSys::SfxPan(unsigned int, unsigned char)
extern "C" void reachstub_412() asm("_ZN9CAudioSys6SfxPanEjh");
extern "C" void reachstub_412() { mpReachStub("_ZN9CAudioSys6SfxPanEjh", "CAudioSys::SfxPan(unsigned int, unsigned char)"); }

// CAudioSys::SfxCtrl(unsigned int, unsigned char, unsigned char)
extern "C" void reachstub_413() asm("_ZN9CAudioSys7SfxCtrlEjhh");
extern "C" void reachstub_413() { mpReachStub("_ZN9CAudioSys7SfxCtrlEjhh", "CAudioSys::SfxCtrl(unsigned int, unsigned char, unsigned char)"); }

// CAudioSys::SfxSpan(unsigned int, unsigned char)
extern "C" void reachstub_414() asm("_ZN9CAudioSys7SfxSpanEjh");
extern "C" void reachstub_414() { mpReachStub("_ZN9CAudioSys7SfxSpanEjh", "CAudioSys::SfxSpan(unsigned int, unsigned char)"); }

// CAudioSys::SfxStop(unsigned int)
extern "C" void reachstub_415() asm("_ZN9CAudioSys7SfxStopEj");
extern "C" void reachstub_415() { mpReachStub("_ZN9CAudioSys7SfxStopEj", "CAudioSys::SfxStop(unsigned int)"); }

// CAudioSys::SfxCheck(unsigned int)
extern "C" void reachstub_416() asm("_ZN9CAudioSys8SfxCheckEj");
extern "C" void reachstub_416() { mpReachStub("_ZN9CAudioSys8SfxCheckEj", "CAudioSys::SfxCheck(unsigned int)"); }

// CAudioSys::SfxStart(unsigned short, unsigned char, unsigned char, unsigned char)
extern "C" void reachstub_417() asm("_ZN9CAudioSys8SfxStartEthhh");
extern "C" void reachstub_417() { mpReachStub("_ZN9CAudioSys8SfxStartEthhh", "CAudioSys::SfxStart(unsigned short, unsigned char, unsigned char, unsigned char)"); }

// CAudioSys::SfxVolume(unsigned int, unsigned char)
extern "C" void reachstub_418() asm("_ZN9CAudioSys9SfxVolumeEjh");
extern "C" void reachstub_418() { mpReachStub("_ZN9CAudioSys9SfxVolumeEjh", "CAudioSys::SfxVolume(unsigned int, unsigned char)"); }

// CGraphics::StreamBegin(ERglPrimitive)
extern "C" void reachstub_421() asm("_ZN9CGraphics11StreamBeginE13ERglPrimitive");
extern "C" void reachstub_421() { mpReachStub("_ZN9CGraphics11StreamBeginE13ERglPrimitive", "CGraphics::StreamBegin(ERglPrimitive)"); }

// CGraphics::StreamVertex(CVector3f const&)
extern "C" void reachstub_423() asm("_ZN9CGraphics12StreamVertexERK9CVector3f");
extern "C" void reachstub_423() { mpReachStub("_ZN9CGraphics12StreamVertexERK9CVector3f", "CGraphics::StreamVertex(CVector3f const&)"); }

// CGraphics::kEnvModulate
extern "C" __attribute__((aligned(32))) char reachdata_424[0x400] asm("_ZN9CGraphics12kEnvModulateE");
__attribute__((aligned(32))) char reachdata_424[0x400] = {};

// CGraphics::kEnvPassthru
extern "C" __attribute__((aligned(32))) char reachdata_425[0x400] asm("_ZN9CGraphics12kEnvPassthruE");
__attribute__((aligned(32))) char reachdata_425[0x400] = {};

// CGraphics::SetAlphaCompare(ERglAlphaFunc, unsigned char, ERglAlphaOp, ERglAlphaFunc, unsigned char)
extern "C" void reachstub_426() asm("_ZN9CGraphics15SetAlphaCompareE13ERglAlphaFunch11ERglAlphaOpS0_h");
extern "C" void reachstub_426() { mpReachStub("_ZN9CGraphics15SetAlphaCompareE13ERglAlphaFunch11ERglAlphaOpS0_h", "CGraphics::SetAlphaCompare(ERglAlphaFunc, unsigned char, ERglAlphaOp, ERglAlphaFunc, unsigned char)"); }

// CGraphics::StreamEnd()
extern "C" void reachstub_430() asm("_ZN9CGraphics9StreamEndEv");
extern "C" void reachstub_430() { mpReachStub("_ZN9CGraphics9StreamEndEv", "CGraphics::StreamEnd()"); }

// CPASDatabase::FindBestAnimation(CPASAnimParmData const&, int) const
extern "C" void reachstub_432() asm("_ZNK12CPASDatabase17FindBestAnimationERK16CPASAnimParmDatai");
extern "C" void reachstub_432() { mpReachStub("_ZNK12CPASDatabase17FindBestAnimationERK16CPASAnimParmDatai", "CPASDatabase::FindBestAnimation(CPASAnimParmData const&, int) const"); }

// CScriptWater::GetWRSurfacePlane() const
extern "C" void reachstub_433() asm("_ZNK12CScriptWater17GetWRSurfacePlaneEv");
extern "C" void reachstub_433() { mpReachStub("_ZNK12CScriptWater17GetWRSurfacePlaneEv", "CScriptWater::GetWRSurfacePlane() const"); }

// CStringTable::GetString(char const*) const
extern "C" void reachstub_434() asm("_ZNK12CStringTable9GetStringEPKc");
extern "C" void reachstub_434() { mpReachStub("_ZNK12CStringTable9GetStringEPKc", "CStringTable::GetString(char const*) const"); }

// CMaterialFilter::Passes(CMaterialList const&) const
extern "C" void reachstub_436() asm("_ZNK15CMaterialFilter6PassesERK13CMaterialList");
extern "C" void reachstub_436() { mpReachStub("_ZNK15CMaterialFilter6PassesERK13CMaterialList", "CMaterialFilter::Passes(CMaterialList const&) const"); }

// CTweakPlayerGun::GetBeamInfo(int) const
extern "C" void reachstub_437() asm("_ZNK15CTweakPlayerGun11GetBeamInfoEi");
extern "C" void reachstub_437() { mpReachStub("_ZNK15CTweakPlayerGun11GetBeamInfoEi", "CTweakPlayerGun::GetBeamInfo(int) const"); }

// CPlayerTargeting::GetScanTargetIndex(CStateManager const&, TUniqueId) const
extern "C" void reachstub_441() asm("_ZNK16CPlayerTargeting18GetScanTargetIndexERK13CStateManager9TUniqueId");
extern "C" void reachstub_441() { mpReachStub("_ZNK16CPlayerTargeting18GetScanTargetIndexERK13CStateManager9TUniqueId", "CPlayerTargeting::GetScanTargetIndex(CStateManager const&, TUniqueId) const"); }

// TReservedAverage<float, 20>::GetAverage() const
extern "C" void reachstub_442() asm("_ZNK16TReservedAverageIfLi20EE10GetAverageEv");
extern "C" void reachstub_442() { mpReachStub("_ZNK16TReservedAverageIfLi20EE10GetAverageEv", "TReservedAverage<float, 20>::GetAverage() const"); }

// CTweakPlayerControls::GetMapping(CControlMapper::ECommands) const
extern "C" void reachstub_443() asm("_ZNK20CTweakPlayerControls10GetMappingEN14CControlMapper9ECommandsE");
extern "C" void reachstub_443() { mpReachStub("_ZNK20CTweakPlayerControls10GetMappingEN14CControlMapper9ECommandsE", "CTweakPlayerControls::GetMapping(CControlMapper::ECommands) const"); }

// CModel::GetAABB() const
extern "C" void reachstub_444() asm("_ZNK6CModel7GetAABBEv");
extern "C" void reachstub_444() { mpReachStub("_ZNK6CModel7GetAABBEv", "CModel::GetAABB() const"); }

// CModel::IsLoaded(int) const
extern "C" void reachstub_445() asm("_ZNK6CModel8IsLoadedEi");
extern "C" void reachstub_445() { mpReachStub("_ZNK6CModel8IsLoadedEi", "CModel::IsLoaded(int) const"); }

// CTexture::Load(GXTexMapID, CTexture::EClampMode) const
extern "C" void reachstub_446() asm("_ZNK8CTexture4LoadE10GXTexMapIDNS_10EClampModeE");
extern "C" void reachstub_446() { mpReachStub("_ZNK8CTexture4LoadE10GXTexMapIDNS_10EClampModeE", "CTexture::Load(GXTexMapID, CTexture::EClampMode) const"); }

// vtable for CCollidableSphere
extern "C" __attribute__((aligned(32))) char reachdata_449[0x400] asm("_ZTV17CCollidableSphere");
__attribute__((aligned(32))) char reachdata_449[0x400] = {};

// fn_80041CCC
extern "C" void reachstub_451() asm("fn_80041CCC");
extern "C" void reachstub_451() { mpReachStub("fn_80041CCC", "fn_80041CCC"); }

// SendMsgs__14CScriptMailboxFRC7TAreaIdR13CStateManager
extern "C" void reachstub_452() asm("SendMsgs__14CScriptMailboxFRC7TAreaIdR13CStateManager");
extern "C" void reachstub_452() { mpReachStub("SendMsgs__14CScriptMailboxFRC7TAreaIdR13CStateManager", "SendMsgs__14CScriptMailboxFRC7TAreaIdR13CStateManager"); }

// fn_80144140
extern "C" void reachstub_453() asm("fn_80144140");
extern "C" void reachstub_453() { mpReachStub("fn_80144140", "fn_80144140"); }

// fn_801ECE14 - carved: defined by src/MetroidPrime/Carve801ECE14.c.  The stub below was a
// duplicate symbol the moment that unit was listed, and `link_gap.py` counts what is missing and
// cannot see a symbol defined twice, so it has to go rather than stay.
// ResetPlayerState__12CEchoEmitterFR13CStateManager
extern "C" void reachstub_454() asm("ResetPlayerState__12CEchoEmitterFR13CStateManager");
extern "C" void reachstub_454() { mpReachStub("ResetPlayerState__12CEchoEmitterFR13CStateManager", "ResetPlayerState__12CEchoEmitterFR13CStateManager"); }

// fn_802CC064
extern "C" void reachstub_456() asm("fn_802CC064");
extern "C" void reachstub_456() { mpReachStub("fn_802CC064", "fn_802CC064"); }

// --- appended by tools/restub_reach.py on 2026-09-29T13:41:25 ---
// Unresolved symbols one boot-probe link asked for. Diagnostic only; see the file header.
// CMemoryCardSys::mIsCardBusy
extern "C" __attribute__((aligned(32))) char reachdata_458[0x400] asm("_ZN14CMemoryCardSys11mIsCardBusyE");
__attribute__((aligned(32))) char reachdata_458[0x400] = {};

// CTextExecuteBuffer::BeginBlock(int, int, int, int, bool, ETextDirection, EJustification, EVerticalJustification)
extern "C" void reachstub_460() asm("_ZN18CTextExecuteBuffer10BeginBlockEiiiib14ETextDirection14EJustification22EVerticalJustification");
extern "C" void reachstub_460() { mpReachStub("_ZN18CTextExecuteBuffer10BeginBlockEiiiib14ETextDirection14EJustification22EVerticalJustification", "CTextExecuteBuffer::BeginBlock(int, int, int, int, bool, ETextDirection, EJustification, EVerticalJustification)"); }

// CTextExecuteBuffer::AddFont(TToken<CRasterFont> const&)
extern "C" void reachstub_461() asm("_ZN18CTextExecuteBuffer7AddFontERK6TTokenI11CRasterFontE");
extern "C" void reachstub_461() { mpReachStub("_ZN18CTextExecuteBuffer7AddFontERK6TTokenI11CRasterFontE", "CTextExecuteBuffer::AddFont(TToken<CRasterFont> const&)"); }

// CTextExecuteBuffer::EndBlock()
extern "C" void reachstub_462() asm("_ZN18CTextExecuteBuffer8EndBlockEv");
extern "C" void reachstub_462() { mpReachStub("_ZN18CTextExecuteBuffer8EndBlockEv", "CTextExecuteBuffer::EndBlock()"); }

// CTextExecuteBuffer::AddString(wchar_t const*, int)
extern "C" void reachstub_463() asm("_ZN18CTextExecuteBuffer9AddStringEPKwi");
extern "C" void reachstub_463() { mpReachStub("_ZN18CTextExecuteBuffer9AddStringEPKwi", "CTextExecuteBuffer::AddString(wchar_t const*, int)"); }

// CTextExecuteBuffer::CTextExecuteBuffer()
extern "C" void reachstub_464() asm("_ZN18CTextExecuteBufferC1Ev");
extern "C" void reachstub_464() { mpReachStub("_ZN18CTextExecuteBufferC1Ev", "CTextExecuteBuffer::CTextExecuteBuffer()"); }

// CGraphics::SetOrtho(float, float, float, float, float, float)
extern "C" void reachstub_467() asm("_ZN9CGraphics8SetOrthoEffffff");
extern "C" void reachstub_467() { mpReachStub("_ZN9CGraphics8SetOrthoEffffff", "CGraphics::SetOrtho(float, float, float, float, float, float)"); }

// CTextRenderBuffer::Render(CColor const&, float) const
extern "C" void reachstub_468() asm("_ZNK17CTextRenderBuffer6RenderERK6CColorf");
extern "C" void reachstub_468() { mpReachStub("_ZNK17CTextRenderBuffer6RenderERK6CColorf", "CTextRenderBuffer::Render(CColor const&, float) const"); }

// CTextExecuteBuffer::BuildRenderBuffer() const
extern "C" void reachstub_469() asm("_ZNK18CTextExecuteBuffer17BuildRenderBufferEv");
extern "C" void reachstub_469() { mpReachStub("_ZNK18CTextExecuteBuffer17BuildRenderBufferEv", "CTextExecuteBuffer::BuildRenderBuffer() const"); }

// gpDefaultFont
extern "C" __attribute__((aligned(32))) char reachdata_470[0x400] asm("gpDefaultFont");
__attribute__((aligned(32))) char reachdata_470[0x400] = {};

// --- appended by tools/restub_reach.py on 2026-09-29T17:43:32 ---
// Unresolved symbols one boot-probe link asked for. Diagnostic only; see the file header.
// GXNtsc480Prog
extern "C" __attribute__((aligned(32))) char reachdata_471[0x400] asm("GXNtsc480Prog");
__attribute__((aligned(32))) char reachdata_471[0x400] = {};

// RETIRED 2026-10-02, stub 472 (`Init__13CTevCombinersFv`). `src/Kyoto/Graphics/CGraphicsSetTevOp.cpp` now
// defines retail's `CTevCombiners::Init` for real (0x802BE51C, 0x6C bytes) and
// `src/Kyoto/Graphics/CGraphicsHostStartup.cpp` calls it under its C++ name, so nothing asks for
// `Init__13CTevCombinersFv` any more. Not the usual duplicate-alias retirement - `restub_reach.py` retires a
// stub whose symbol another object defines, and the real definition here is
// `_ZN13CTevCombiners4InitEv`, a different symbol, so nothing was ever going to collide. Retired
// anyway: a stub that logs `Init__13CTevCombinersFv` is a claim about what the boot path demands, and it is
// no longer one. `docs/research/boot_path_reachable.tsv` never listed this symbol (checked), so
// no reachability input needs regenerating. Same shape as the retirement of
// `_ZN11CSimplePool11fn_8029c7e8ERK10SObjectTag` at 2026-09-27.


// --- appended by tools/restub_reach.py on 2026-09-30T00:07:19 ---
// Unresolved symbols one boot-probe link asked for. Diagnostic only; see the file header.
// RETIRED 2026-09-30. src/MetroidPrime/Weapons/GunController/CGunMotion.cpp defines
// `CGunMotion::LoadAnimations()` for real, so this alias is a duplicate under -DMP_BOOT_STUBS=ON -
// the only configuration tools/boot_probe.sh builds. Same shape as the two retirements above.

// NWeaponTypes::are_tokens_ready(rstl::vector<CToken, rstl::rmemory_allocator> const&)
extern "C" void reachstub_475() asm("_ZN12NWeaponTypes16are_tokens_readyERKN4rstl6vectorI6CTokenNS0_17rmemory_allocatorEEE");
extern "C" void reachstub_475() { mpReachStub("_ZN12NWeaponTypes16are_tokens_readyERKN4rstl6vectorI6CTokenNS0_17rmemory_allocatorEEE", "NWeaponTypes::are_tokens_ready(rstl::vector<CToken, rstl::rmemory_allocator> const&)"); }

// CGameProjectile::GetBeamAttribType(EWeaponType)
extern "C" void reachstub_476() asm("_ZN15CGameProjectile17GetBeamAttribTypeE11EWeaponType");
extern "C" void reachstub_476() { mpReachStub("_ZN15CGameProjectile17GetBeamAttribTypeE11EWeaponType", "CGameProjectile::GetBeamAttribType(EWeaponType)"); }

// CCollidableAABox::~CCollidableAABox()
extern "C" void reachstub_477() asm("_ZN16CCollidableAABoxD1Ev");
extern "C" void reachstub_477() { mpReachStub("_ZN16CCollidableAABoxD1Ev", "CCollidableAABox::~CCollidableAABox()"); }

// CCollidableSphere::~CCollidableSphere()
extern "C" void reachstub_478() asm("_ZN17CCollidableSphereD1Ev");
extern "C" void reachstub_478() { mpReachStub("_ZN17CCollidableSphereD1Ev", "CCollidableSphere::~CCollidableSphere()"); }

// CPASDatabase::GetAnimState(int) const
extern "C" void reachstub_479() asm("_ZNK12CPASDatabase12GetAnimStateEi");
extern "C" void reachstub_479() { mpReachStub("_ZNK12CPASDatabase12GetAnimStateEi", "CPASDatabase::GetAnimState(int) const"); }

// CPASDatabase::FindBestAnimation(CPASAnimParmData const&, CRandom16&, int) const
extern "C" void reachstub_480() asm("_ZNK12CPASDatabase17FindBestAnimationERK16CPASAnimParmDataR9CRandom16i");
extern "C" void reachstub_480() { mpReachStub("_ZNK12CPASDatabase17FindBestAnimationERK16CPASAnimParmDataR9CRandom16i", "CPASDatabase::FindBestAnimation(CPASAnimParmData const&, CRandom16&, int) const"); }

// --- appended by tools/restub_reach.py on 2026-10-01T09:36:05 ---
// Unresolved symbols one boot-probe link asked for. Diagnostic only; see the file header.
// CScriptGrapplePoint* TCastToPtr<CScriptGrapplePoint>(CEntity*)
extern "C" void reachstub_481() asm("_Z10TCastToPtrI19CScriptGrapplePointEPT_P7CEntity");
extern "C" void reachstub_481() { mpReachStub("_Z10TCastToPtrI19CScriptGrapplePointEPT_P7CEntity", "CScriptGrapplePoint* TCastToPtr<CScriptGrapplePoint>(CEntity*)"); }

// LdrToDamageInfo(SLdrDamageInfo const&)
extern "C" void reachstub_482() asm("_Z15LdrToDamageInfoRK14SLdrDamageInfo");
extern "C" void reachstub_482() { mpReachStub("_Z15LdrToDamageInfoRK14SLdrDamageInfo", "LdrToDamageInfo(SLdrDamageInfo const&)"); }

// CDamageInfo::CDamageInfo(SLdrTDamageInfo const&, bool, bool, bool, bool)
extern "C" void reachstub_483() asm("_ZN11CDamageInfoC1ERK15SLdrTDamageInfobbbb");
extern "C" void reachstub_483() { mpReachStub("_ZN11CDamageInfoC1ERK15SLdrTDamageInfobbbb", "CDamageInfo::CDamageInfo(SLdrTDamageInfo const&, bool, bool, bool, bool)"); }

// CStateManager::InformListeners(CVector3f const&, EListenNoiseType)
extern "C" void reachstub_484() asm("_ZN13CStateManager15InformListenersERK9CVector3f16EListenNoiseType");
extern "C" void reachstub_484() { mpReachStub("_ZN13CStateManager15InformListenersERK9CVector3f16EListenNoiseType", "CStateManager::InformListeners(CVector3f const&, EListenNoiseType)"); }

// CGameSplineDesc::CGameSplineDesc(CMayaSpline const&, CMotionSpline::ESplineType, float, bool)
extern "C" void reachstub_485() asm("_ZN15CGameSplineDescC1ERK11CMayaSplineN13CMotionSpline11ESplineTypeEfb");
extern "C" void reachstub_485() { mpReachStub("_ZN15CGameSplineDescC1ERK11CMayaSplineN13CMotionSpline11ESplineTypeEfb", "CGameSplineDesc::CGameSplineDesc(CMayaSpline const&, CMotionSpline::ESplineType, float, bool)"); }

// CPathFindSearch::Search(CVector3f const&, CVector3f const&)
extern "C" void reachstub_486() asm("_ZN15CPathFindSearch6SearchERK9CVector3fS2_");
extern "C" void reachstub_486() { mpReachStub("_ZN15CPathFindSearch6SearchERK9CVector3fS2_", "CPathFindSearch::Search(CVector3f const&, CVector3f const&)"); }

// CGameCamera::ConvertToScreenSpace(CVector3f const&) const
extern "C" void reachstub_487() asm("_ZNK11CGameCamera20ConvertToScreenSpaceERK9CVector3f");
extern "C" void reachstub_487() { mpReachStub("_ZNK11CGameCamera20ConvertToScreenSpaceERK9CVector3f", "CGameCamera::ConvertToScreenSpace(CVector3f const&) const"); }

// CGameCamera::GetFov() const
extern "C" void reachstub_488() asm("_ZNK11CGameCamera6GetFovEv");
extern "C" void reachstub_488() { mpReachStub("_ZNK11CGameCamera6GetFovEv", "CGameCamera::GetFov() const"); }

// CBodyStateInfo::GetCurrentState() const
extern "C" void reachstub_489() asm("_ZNK14CBodyStateInfo15GetCurrentStateEv");
extern "C" void reachstub_489() { mpReachStub("_ZNK14CBodyStateInfo15GetCurrentStateEv", "CBodyStateInfo::GetCurrentState() const"); }

// fn_8018A7E8
extern "C" void reachstub_490() asm("fn_8018A7E8");
extern "C" void reachstub_490() { mpReachStub("fn_8018A7E8", "fn_8018A7E8"); }

// fn_8018A8D0
extern "C" void reachstub_491() asm("fn_8018A8D0");
extern "C" void reachstub_491() { mpReachStub("fn_8018A8D0", "fn_8018A8D0"); }

// lbl_8041C4F4
extern "C" __attribute__((aligned(32))) char reachdata_492[0x400] asm("lbl_8041C4F4");
__attribute__((aligned(32))) char reachdata_492[0x400] = {};

// --- appended by tools/restub_reach.py on 2026-10-01T09:43:56 ---
// Unresolved symbols one boot-probe link asked for. Diagnostic only; see the file header.
// --- appended by tools/restub_reach.py on 2026-10-01T09:48:18 ---
// Unresolved symbols one boot-probe link asked for. Diagnostic only; see the file header.
// CWorldState::~CWorldState()
extern "C" void reachstub_511() asm("_ZN11CWorldStateD1Ev");
extern "C" void reachstub_511() { mpReachStub("_ZN11CWorldStateD1Ev", "CWorldState::~CWorldState()"); }

// CGMDeathMatch::CGMDeathMatch(int, int, float, bool, bool)
extern "C" void reachstub_512() asm("_ZN13CGMDeathMatchC1Eiifbb");
extern "C" void reachstub_512() { mpReachStub("_ZN13CGMDeathMatchC1Eiifbb", "CGMDeathMatch::CGMDeathMatch(int, int, float, bool, bool)"); }

// CMapWorldInfo::CMapWorldInfo(CBitStreamReader&, CWorldSaveGameInfo const&, unsigned int)
extern "C" void reachstub_513() asm("_ZN13CMapWorldInfoC1ER16CBitStreamReaderRK18CWorldSaveGameInfoj");
extern "C" void reachstub_513() { mpReachStub("_ZN13CMapWorldInfoC1ER16CBitStreamReaderRK18CWorldSaveGameInfoj", "CMapWorldInfo::CMapWorldInfo(CBitStreamReader&, CWorldSaveGameInfo const&, unsigned int)"); }

// CMapWorldInfo::CMapWorldInfo()
extern "C" void reachstub_514() asm("_ZN13CMapWorldInfoC1Ev");
extern "C" void reachstub_514() { mpReachStub("_ZN13CMapWorldInfoC1Ev", "CMapWorldInfo::CMapWorldInfo()"); }

// CRelayTracker::CRelayTracker(CBitStreamReader&, CWorldSaveGameInfo const&)
extern "C" void reachstub_515() asm("_ZN13CRelayTrackerC1ER16CBitStreamReaderRK18CWorldSaveGameInfo");
extern "C" void reachstub_515() { mpReachStub("_ZN13CRelayTrackerC1ER16CBitStreamReaderRK18CWorldSaveGameInfo", "CRelayTracker::CRelayTracker(CBitStreamReader&, CWorldSaveGameInfo const&)"); }

// CRelayTracker::CRelayTracker()
extern "C" void reachstub_516() asm("_ZN13CRelayTrackerC1Ev");
extern "C" void reachstub_516() { mpReachStub("_ZN13CRelayTrackerC1Ev", "CRelayTracker::CRelayTracker()"); }

// CRelayTracker::~CRelayTracker()
extern "C" void reachstub_517() asm("_ZN13CRelayTrackerD1Ev");
extern "C" void reachstub_517() { mpReachStub("_ZN13CRelayTrackerD1Ev", "CRelayTracker::~CRelayTracker()"); }

// CGMMultiplayer::SetMusicIndex(int)
extern "C" void reachstub_518() asm("_ZN14CGMMultiplayer13SetMusicIndexEi");
extern "C" void reachstub_518() { mpReachStub("_ZN14CGMMultiplayer13SetMusicIndexEi", "CGMMultiplayer::SetMusicIndex(int)"); }

// CWorldTransManager::CWorldTransManager()
extern "C" void reachstub_520() asm("_ZN18CWorldTransManagerC1Ev");
extern "C" void reachstub_520() { mpReachStub("_ZN18CWorldTransManagerC1Ev", "CWorldTransManager::CWorldTransManager()"); }

// CWorldTransManager::~CWorldTransManager()
extern "C" void reachstub_521() asm("_ZN18CWorldTransManagerD1Ev");
extern "C" void reachstub_521() { mpReachStub("_ZN18CWorldTransManagerD1Ev", "CWorldTransManager::~CWorldTransManager()"); }

// CGameStateEnvVarManager::LoadFields() was reachstub_522 here. Its body is retail 0x80145C98 -
// the eleven default persistent options - and it now lives in
// src/MetroidPrime/Player/CGameState.cpp, under the class's own name, where the header declares
// it and both constructors call it. Keeping the alias would be a duplicate definition under
// MP_BOOT_STUBS=ON, the one link failure the ordinary port link cannot see.

// SPersistentOptionsValue::SPersistentOptionsValue(int, int, int) was reachstub_523 here, and the
// boot hit it eleven times per run. Its body is retail 0x801462DC - store the three arguments and
// call the clamp at 0x801461AC - and it now lives in
// src/MetroidPrime/Player/PortSPersistentOptionsValue.cpp, under the class's own name, so the host
// compiler's `_ZN23SPersistentOptionsValueC1Eiii` resolves to retail's constructor. Keeping the
// alias would be a duplicate definition under MP_BOOT_STUBS=ON, the one link failure the ordinary
// port link cannot see.

// CMain::StreamNewGameState(bool)
extern "C" void reachstub_524() asm("_ZN5CMain18StreamNewGameStateEb");
extern "C" void reachstub_524() { mpReachStub("_ZN5CMain18StreamNewGameStateEb", "CMain::StreamNewGameState(bool)"); }

// CMain::EnsureWorldPakReady(unsigned int)
extern "C" void reachstub_525() asm("_ZN5CMain19EnsureWorldPakReadyEj");
extern "C" void reachstub_525() { mpReachStub("_ZN5CMain19EnsureWorldPakReadyEj", "CMain::EnsureWorldPakReady(unsigned int)"); }

// CGMCoin::CGMCoin(int, int, float, bool)
extern "C" void reachstub_526() asm("_ZN7CGMCoinC1Eiifb");
extern "C" void reachstub_526() { mpReachStub("_ZN7CGMCoinC1Eiifb", "CGMCoin::CGMCoin(int, int, float, bool)"); }

// CTweakGame::GetHardModeDamageMultiplier() const
extern "C" void reachstub_527() asm("_ZNK10CTweakGame27GetHardModeDamageMultiplierEv");
extern "C" void reachstub_527() { mpReachStub("_ZNK10CTweakGame27GetHardModeDamageMultiplierEv", "CTweakGame::GetHardModeDamageMultiplier() const"); }

// CTweakGame::GetHardModeWeaponMultiplier() const
extern "C" void reachstub_528() asm("_ZNK10CTweakGame27GetHardModeWeaponMultiplierEv");
extern "C" void reachstub_528() { mpReachStub("_ZNK10CTweakGame27GetHardModeWeaponMultiplierEv", "CTweakGame::GetHardModeWeaponMultiplier() const"); }

// CMapWorldInfo::PutTo(CBitStreamWriter&, CWorldSaveGameInfo const&, unsigned int) const
extern "C" void reachstub_540() asm("_ZNK13CMapWorldInfo5PutToER16CBitStreamWriterRK18CWorldSaveGameInfoj");
extern "C" void reachstub_540() { mpReachStub("_ZNK13CMapWorldInfo5PutToER16CBitStreamWriterRK18CWorldSaveGameInfoj", "CMapWorldInfo::PutTo(CBitStreamWriter&, CWorldSaveGameInfo const&, unsigned int) const"); }

// CRelayTracker::PutTo(CBitStreamWriter&, CWorldSaveGameInfo const&) const
extern "C" void reachstub_541() asm("_ZNK13CRelayTracker5PutToER16CBitStreamWriterRK18CWorldSaveGameInfo");
extern "C" void reachstub_541() { mpReachStub("_ZNK13CRelayTracker5PutToER16CBitStreamWriterRK18CWorldSaveGameInfo", "CRelayTracker::PutTo(CBitStreamWriter&, CWorldSaveGameInfo const&) const"); }

// __ct__12CGameOptionsFR16CBitStreamReader
extern "C" void reachstub_542() asm("__ct__12CGameOptionsFR16CBitStreamReader");
extern "C" void reachstub_542() { mpReachStub("__ct__12CGameOptionsFR16CBitStreamReader", "__ct__12CGameOptionsFR16CBitStreamReader"); }

// fn_80003D00
extern "C" void reachstub_543() asm("fn_80003D00");
extern "C" void reachstub_543() { mpReachStub("fn_80003D00", "fn_80003D00"); }

// fn_80004458
extern "C" void reachstub_544() asm("fn_80004458");
extern "C" void reachstub_544() { mpReachStub("fn_80004458", "fn_80004458"); }

// fn_80004D84
extern "C" void reachstub_546() asm("fn_80004D84");
extern "C" void reachstub_546() { mpReachStub("fn_80004D84", "fn_80004D84"); }

// fn_800068F4
extern "C" void reachstub_547() asm("fn_800068F4");
extern "C" void reachstub_547() { mpReachStub("fn_800068F4", "fn_800068F4"); }

// RETIRED 2026-10-01 (lane `L2`). src/MetroidPrime/PortCPersistentOptionsMap.cpp now defines
// fn_80145ACC for real, so this alias is a duplicate under MP_BOOT_STUBS=ON - the configuration
// only tools/boot_probe.sh uses, and the one the gate's `port link dups` step cannot see. Not in
// docs/research/boot_path_reachable.tsv either, so the generator will not put it back; this entry
// was appended by tools/restub_reach.py from a failed probe link.

// lbl_803A91C8
extern "C" __attribute__((aligned(32))) char reachdata_549[0x400] asm("lbl_803A91C8");
__attribute__((aligned(32))) char reachdata_549[0x400] = {};

// --- appended by tools/restub_reach.py on 2026-10-01T09:49:06 ---
// Unresolved symbols one boot-probe link asked for. Diagnostic only; see the file header.
// CScriptDock* TCastToPtr<CScriptDock>(CEntity*)
extern "C" void reachstub_550() asm("_Z10TCastToPtrI11CScriptDockEPT_P7CEntity");
extern "C" void reachstub_550() { mpReachStub("_Z10TCastToPtrI11CScriptDockEPT_P7CEntity", "CScriptDock* TCastToPtr<CScriptDock>(CEntity*)"); }

// CPVSAreaSet::MakeAreaSet(char const*, int)
extern "C" void reachstub_551() asm("_ZN11CPVSAreaSet11MakeAreaSetEPKci");
extern "C" void reachstub_551() { mpReachStub("_ZN11CPVSAreaSet11MakeAreaSetEPKci", "CPVSAreaSet::MakeAreaSet(char const*, int)"); }

// CPortalArea::CPortalArea(TLockedToken<CPortalAreaData> const&)
extern "C" void reachstub_552() asm("_ZN11CPortalAreaC1ERK12TLockedTokenI15CPortalAreaDataE");
extern "C" void reachstub_552() { mpReachStub("_ZN11CPortalAreaC1ERK12TLockedTokenI15CPortalAreaDataE", "CPortalArea::CPortalArea(TLockedToken<CPortalAreaData> const&)"); }

// CScriptDock::InitializeConnectedArea(CStateManager&)
extern "C" void reachstub_553() asm("_ZN11CScriptDock23InitializeConnectedAreaER13CStateManager");
extern "C" void reachstub_553() { mpReachStub("_ZN11CScriptDock23InitializeConnectedAreaER13CStateManager", "CScriptDock::InitializeConnectedArea(CStateManager&)"); }

// CWorldLight::CWorldLight(CInputStream&)
extern "C" void reachstub_554() asm("_ZN11CWorldLightC1ER12CInputStream");
extern "C" void reachstub_554() { mpReachStub("_ZN11CWorldLightC1ER12CInputStream", "CWorldLight::CWorldLight(CInputStream&)"); }

// CAreaBspTree::CAreaBspTree(CInputStream&, CTransform4f const&)
extern "C" void reachstub_555() asm("_ZN12CAreaBspTreeC1ER12CInputStreamRK12CTransform4f");
extern "C" void reachstub_555() { mpReachStub("_ZN12CAreaBspTreeC1ER12CInputStreamRK12CTransform4f", "CAreaBspTree::CAreaBspTree(CInputStream&, CTransform4f const&)"); }

// CAreaOctTree::MakeFromMemory(void*, unsigned int, CAreaOctTree**, bool*)
extern "C" void reachstub_556() asm("_ZN12CAreaOctTree14MakeFromMemoryEPvjPPS_Pb");
extern "C" void reachstub_556() { mpReachStub("_ZN12CAreaOctTree14MakeFromMemoryEPvjPPS_Pb", "CAreaOctTree::MakeFromMemory(void*, unsigned int, CAreaOctTree**, bool*)"); }

// SAreaSurface::SAreaSurface(CInputStream&)
extern "C" void reachstub_557() asm("_ZN12SAreaSurfaceC1ER12CInputStream");
extern "C" void reachstub_557() { mpReachStub("_ZN12SAreaSurfaceC1ER12CInputStream", "SAreaSurface::SAreaSurface(CInputStream&)"); }

// CStateManager::PrepareAreaUnload(TAreaId)
extern "C" void reachstub_562() asm("_ZN13CStateManager17PrepareAreaUnloadE7TAreaId");
extern "C" void reachstub_562() { mpReachStub("_ZN13CStateManager17PrepareAreaUnloadE7TAreaId", "CStateManager::PrepareAreaUnload(TAreaId)"); }

// CAreaRenderOctTree::CAreaRenderOctTree(rstl::auto_ptr<unsigned char const> const&)
extern "C" void reachstub_564() asm("_ZN18CAreaRenderOctTreeC1ERKN4rstl8auto_ptrIKhEE");
extern "C" void reachstub_564() { mpReachStub("_ZN18CAreaRenderOctTreeC1ERKN4rstl8auto_ptrIKhEE", "CAreaRenderOctTree::CAreaRenderOctTree(rstl::auto_ptr<unsigned char const> const&)"); }

// CStaticGeometryMap::CStaticGeometryMap(TLockedToken<CStaticGeometryMapData> const&)
extern "C" void reachstub_565() asm("_ZN18CStaticGeometryMapC1ERK12TLockedTokenI22CStaticGeometryMapDataE");
extern "C" void reachstub_565() { mpReachStub("_ZN18CStaticGeometryMapC1ERK12TLockedTokenI22CStaticGeometryMapDataE", "CStaticGeometryMap::CStaticGeometryMap(TLockedToken<CStaticGeometryMapData> const&)"); }

// CInGameTweakManager::GetIdentifierForWorldDefaultMusic(unsigned int)
extern "C" void reachstub_566() asm("_ZN19CInGameTweakManager33GetIdentifierForWorldDefaultMusicEj");
extern "C" void reachstub_566() { mpReachStub("_ZN19CInGameTweakManager33GetIdentifierForWorldDefaultMusicEj", "CInGameTweakManager::GetIdentifierForWorldDefaultMusic(unsigned int)"); }

// CMetroidModelInstance::CMetroidModelInstance(void const*, void const*, void const*, void const*, void const*, void const*, void const*, rstl::vector<void*, rstl::rmemory_allocator> const&, void const* const&, void const* const&)
extern "C" void reachstub_567() asm("_ZN21CMetroidModelInstanceC1EPKvS1_S1_S1_S1_S1_S1_RKN4rstl6vectorIPvNS2_17rmemory_allocatorEEERKS1_SA_");
extern "C" void reachstub_567() { mpReachStub("_ZN21CMetroidModelInstanceC1EPKvS1_S1_S1_S1_S1_S1_RKN4rstl6vectorIPvNS2_17rmemory_allocatorEEERKS1_SA_", "CMetroidModelInstance::CMetroidModelInstance(void const*, void const*, void const*, void const*, void const*, void const*, void const*, rstl::vector<void*, rstl::rmemory_allocator> const&, void const* const&, void const* const&)"); }

// CCollisionPrimitiveData::~CCollisionPrimitiveData()
extern "C" void reachstub_568() asm("_ZN23CCollisionPrimitiveDataD2Ev");
extern "C" void reachstub_568() { mpReachStub("_ZN23CCollisionPrimitiveDataD2Ev", "CCollisionPrimitiveData::~CCollisionPrimitiveData()"); }

// CScriptObjectLoaderHelper::SLoadContext::SLoadContext(TAreaId)
extern "C" void reachstub_569() asm("_ZN25CScriptObjectLoaderHelper12SLoadContextC1E7TAreaId");
extern "C" void reachstub_569() { mpReachStub("_ZN25CScriptObjectLoaderHelper12SLoadContextC1E7TAreaId", "CScriptObjectLoaderHelper::SLoadContext::SLoadContext(TAreaId)"); }

// CScriptObjectLoaderHelper::BeginLayerLoad(CScriptObjectLoaderHelper::SLoadContext&, rstl::auto_ptr<CInputStream>, rstl::vector<TEditorId, rstl::rmemory_allocator>&)
extern "C" void reachstub_570() asm("_ZN25CScriptObjectLoaderHelper14BeginLayerLoadERNS_12SLoadContextEN4rstl8auto_ptrI12CInputStreamEERNS2_6vectorI9TEditorIdNS2_17rmemory_allocatorEEE");
extern "C" void reachstub_570() { mpReachStub("_ZN25CScriptObjectLoaderHelper14BeginLayerLoadERNS_12SLoadContextEN4rstl8auto_ptrI12CInputStreamEERNS2_6vectorI9TEditorIdNS2_17rmemory_allocatorEEE", "CScriptObjectLoaderHelper::BeginLayerLoad(CScriptObjectLoaderHelper::SLoadContext&, rstl::auto_ptr<CInputStream>, rstl::vector<TEditorId, rstl::rmemory_allocator>&)"); }

// CScriptObjectLoaderHelper::ContinueLayerLoad(CScriptObjectLoaderHelper::SLoadContext&, unsigned int, CStateManager&)
extern "C" void reachstub_571() asm("_ZN25CScriptObjectLoaderHelper17ContinueLayerLoadERNS_12SLoadContextEjR13CStateManager");
extern "C" void reachstub_571() { mpReachStub("_ZN25CScriptObjectLoaderHelper17ContinueLayerLoadERNS_12SLoadContextEjR13CStateManager", "CScriptObjectLoaderHelper::ContinueLayerLoad(CScriptObjectLoaderHelper::SLoadContext&, unsigned int, CStateManager&)"); }

// CScriptObjectLoaderHelper::InitScriptObjects(rstl::vector<TEditorId, rstl::rmemory_allocator>&, CStateManager&)
extern "C" void reachstub_572() asm("_ZN25CScriptObjectLoaderHelper17InitScriptObjectsERN4rstl6vectorI9TEditorIdNS0_17rmemory_allocatorEEER13CStateManager");
extern "C" void reachstub_572() { mpReachStub("_ZN25CScriptObjectLoaderHelper17InitScriptObjectsERN4rstl6vectorI9TEditorIdNS0_17rmemory_allocatorEEER13CStateManager", "CScriptObjectLoaderHelper::InitScriptObjects(rstl::vector<TEditorId, rstl::rmemory_allocator>&, CStateManager&)"); }

// CScriptObjectLoaderHelper::LoadScriptObjects(TAreaId, CInputStream&, rstl::vector<TEditorId, rstl::rmemory_allocator>&, CStateManager&)
extern "C" void reachstub_573() asm("_ZN25CScriptObjectLoaderHelper17LoadScriptObjectsE7TAreaIdR12CInputStreamRN4rstl6vectorI9TEditorIdNS3_17rmemory_allocatorEEER13CStateManager");
extern "C" void reachstub_573() { mpReachStub("_ZN25CScriptObjectLoaderHelper17LoadScriptObjectsE7TAreaIdR12CInputStreamRN4rstl6vectorI9TEditorIdNS3_17rmemory_allocatorEEER13CStateManager", "CScriptObjectLoaderHelper::LoadScriptObjects(TAreaId, CInputStream&, rstl::vector<TEditorId, rstl::rmemory_allocator>&, CStateManager&)"); }

// CScriptObjectLoaderHelper::RemoveLayerObjects(TAreaId, TLayerId, CStateManager&)
extern "C" void reachstub_574() asm("_ZN25CScriptObjectLoaderHelper18RemoveLayerObjectsE7TAreaId8TLayerIdR13CStateManager");
extern "C" void reachstub_574() { mpReachStub("_ZN25CScriptObjectLoaderHelper18RemoveLayerObjectsE7TAreaId8TLayerIdR13CStateManager", "CScriptObjectLoaderHelper::RemoveLayerObjects(TAreaId, TLayerId, CStateManager&)"); }

// CScriptObjectLoaderHelper::RegisterScriptObjects(rstl::vector<CEntity*, rstl::rmemory_allocator>, CStateManager&)
extern "C" void reachstub_575() asm("_ZN25CScriptObjectLoaderHelper21RegisterScriptObjectsEN4rstl6vectorIP7CEntityNS0_17rmemory_allocatorEEER13CStateManager");
extern "C" void reachstub_575() { mpReachStub("_ZN25CScriptObjectLoaderHelper21RegisterScriptObjectsEN4rstl6vectorIP7CEntityNS0_17rmemory_allocatorEEER13CStateManager", "CScriptObjectLoaderHelper::RegisterScriptObjects(rstl::vector<CEntity*, rstl::rmemory_allocator>, CStateManager&)"); }

// CScriptObjectLoaderHelper::LoadGeneratedScriptObjects(TAreaId, CInputStream&)
extern "C" void reachstub_576() asm("_ZN25CScriptObjectLoaderHelper26LoadGeneratedScriptObjectsE7TAreaIdR12CInputStream");
extern "C" void reachstub_576() { mpReachStub("_ZN25CScriptObjectLoaderHelper26LoadGeneratedScriptObjectsE7TAreaIdR12CInputStream", "CScriptObjectLoaderHelper::LoadGeneratedScriptObjects(TAreaId, CInputStream&)"); }

// CPFArea::SetTransform(CTransform4f const&)
extern "C" void reachstub_577() asm("_ZN7CPFArea12SetTransformERK12CTransform4f");
extern "C" void reachstub_577() { mpReachStub("_ZN7CPFArea12SetTransformERK12CTransform4f", "CPFArea::SetTransform(CTransform4f const&)"); }

// CGameArea::CPostConstructed::CPostConstructed(CGameArea const&)
extern "C" void reachstub_578() asm("_ZN9CGameArea16CPostConstructedC1ERKS_");
extern "C" void reachstub_578() { mpReachStub("_ZN9CGameArea16CPostConstructedC1ERKS_", "CGameArea::CPostConstructed::CPostConstructed(CGameArea const&)"); }

// CGameArea::CPostConstructed::~CPostConstructed()
extern "C" void reachstub_579() asm("_ZN9CGameArea16CPostConstructedD1Ev");
extern "C" void reachstub_579() { mpReachStub("_ZN9CGameArea16CPostConstructedD1Ev", "CGameArea::CPostConstructed::~CPostConstructed()"); }

// CGraphics::SetFog(ERglFogMode, float, float, CColor const&)
extern "C" void reachstub_580() asm("_ZN9CGraphics6SetFogE11ERglFogModeffRK6CColor");
extern "C" void reachstub_580() { mpReachStub("_ZN9CGraphics6SetFogE11ERglFogModeffRK6CColor", "CGraphics::SetFog(ERglFogMode, float, float, CColor const&)"); }

// CMapWorld::SetWhichMapAreasLoaded(IWorld const&, int, int)
extern "C" void reachstub_581() asm("_ZN9CMapWorld22SetWhichMapAreasLoadedERK6IWorldii");
extern "C" void reachstub_581() { mpReachStub("_ZN9CMapWorld22SetWhichMapAreasLoadedERK6IWorldii", "CMapWorld::SetWhichMapAreasLoaded(IWorld const&, int, int)"); }

// CPVSAreaSet::GetEntityIdByIndex(unsigned int) const
extern "C" void reachstub_582() asm("_ZNK11CPVSAreaSet18GetEntityIdByIndexEj");
extern "C" void reachstub_582() { mpReachStub("_ZNK11CPVSAreaSet18GetEntityIdByIndexEj", "CPVSAreaSet::GetEntityIdByIndex(unsigned int) const"); }

// CWorldLight::GetAsCGraphicsLight() const
extern "C" void reachstub_583() asm("_ZNK11CWorldLight19GetAsCGraphicsLightEv");
extern "C" void reachstub_583() { mpReachStub("_ZNK11CWorldLight19GetAsCGraphicsLightEv", "CWorldLight::GetAsCGraphicsLight() const"); }

// CTexture::LoadToARAM() const
extern "C" void reachstub_586() asm("_ZNK8CTexture10LoadToARAMEv");
extern "C" void reachstub_586() { mpReachStub("_ZNK8CTexture10LoadToARAMEv", "CTexture::LoadToARAM() const"); }

// CTexture::LoadToMRAM() const
extern "C" void reachstub_587() asm("_ZNK8CTexture10LoadToMRAMEv");
extern "C" void reachstub_587() { mpReachStub("_ZNK8CTexture10LoadToMRAMEv", "CTexture::LoadToMRAM() const"); }

// CTexture::MakeSwappable() const
extern "C" void reachstub_588() asm("_ZNK8CTexture13MakeSwappableEv");
extern "C" void reachstub_588() { mpReachStub("_ZNK8CTexture13MakeSwappableEv", "CTexture::MakeSwappable() const"); }

// CTexture::IsARAMTransferInProgress() const
extern "C" void reachstub_589() asm("_ZNK8CTexture24IsARAMTransferInProgressEv");
extern "C" void reachstub_589() { mpReachStub("_ZNK8CTexture24IsARAMTransferInProgressEv", "CTexture::IsARAMTransferInProgress() const"); }

// CMapWorld::IsMapAreasStreaming() const
extern "C" void reachstub_590() asm("_ZNK9CMapWorld19IsMapAreasStreamingEv");
extern "C" void reachstub_590() { mpReachStub("_ZNK9CMapWorld19IsMapAreasStreamingEv", "CMapWorld::IsMapAreasStreaming() const"); }

// --- appended by tools/restub_reach.py on 2026-10-01T21:08:07 ---
// Unresolved symbols one boot-probe link asked for. Diagnostic only; see the file header.
// CScriptPlatform* TCastToPtr<CScriptPlatform>(CEntity*)
extern "C" void reachstub_591() asm("_Z10TCastToPtrI15CScriptPlatformEPT_P7CEntity");
extern "C" void reachstub_591() { mpReachStub("_Z10TCastToPtrI15CScriptPlatformEPT_P7CEntity", "CScriptPlatform* TCastToPtr<CScriptPlatform>(CEntity*)"); }

// ReadPlayerItem(int&, CInputStream&)
extern "C" void reachstub_592() asm("_Z14ReadPlayerItemRiR12CInputStream");
extern "C" void reachstub_592() { mpReachStub("_Z14ReadPlayerItemRiR12CInputStream", "ReadPlayerItem(int&, CInputStream&)"); }

// GetLockedCacheAllocationBase()
extern "C" void reachstub_593() asm("_Z28GetLockedCacheAllocationBasev");
extern "C" void reachstub_593() { mpReachStub("_Z28GetLockedCacheAllocationBasev", "GetLockedCacheAllocationBase()"); }

// CGameCollision::Move(CStateManager&, CPhysicsActor&, float, rstl::reserved_vector< TUniqueId, kMaxObjects > const*)
extern "C" void reachstub_594() asm("_ZN14CGameCollision4MoveER13CStateManagerR13CPhysicsActorfPKN4rstl15reserved_vectorI9TUniqueIdLi1024EEE");
extern "C" void reachstub_594() { mpReachStub("_ZN14CGameCollision4MoveER13CStateManagerR13CPhysicsActorfPKN4rstl15reserved_vectorI9TUniqueIdLi1024EEE", "CGameCollision::Move(CStateManager&, CPhysicsActor&, float, rstl::reserved_vector< TUniqueId, kMaxObjects > const*)"); }

// CInGameTweakManager::~CInGameTweakManager()
extern "C" void reachstub_595() asm("_ZN19CInGameTweakManagerD1Ev");
extern "C" void reachstub_595() { mpReachStub("_ZN19CInGameTweakManagerD1Ev", "CInGameTweakManager::~CInGameTweakManager()"); }

// CBasics::CopyMemory(void*, void const*, unsigned int)
extern "C" void reachstub_596() asm("_ZN7CBasics10CopyMemoryEPvPKvj");
extern "C" void reachstub_596() { mpReachStub("_ZN7CBasics10CopyMemoryEPvPKvj", "CBasics::CopyMemory(void*, void const*, unsigned int)"); }

// --- appended by tools/restub_reach.py on 2026-10-02T16:46:36 ---
// Unresolved symbols one boot-probe link asked for. Diagnostic only; see the file header.
// CGameHint* TCastToPtr<CGameHint>(CEntity*)
extern "C" void reachstub_597() asm("_Z10TCastToPtrI9CGameHintEPT_P7CEntity");
extern "C" void reachstub_597() { mpReachStub("_Z10TCastToPtrI9CGameHintEPT_P7CEntity", "CGameHint* TCastToPtr<CGameHint>(CEntity*)"); }

// LdrToModelData(CVector3f const&, unsigned int, SLdrAnimationSet const&, bool)
extern "C" void reachstub_598() asm("_Z14LdrToModelDataRK9CVector3fjRK16SLdrAnimationSetb");
extern "C" void reachstub_598() { mpReachStub("_Z14LdrToModelDataRK9CVector3fjRK16SLdrAnimationSetb", "LdrToModelData(CVector3f const&, unsigned int, SLdrAnimationSet const&, bool)"); }

// LdrToTransform4f(SLdrEditorProperties const&)
extern "C" void reachstub_599() asm("_Z16LdrToTransform4fRK20SLdrEditorProperties");
extern "C" void reachstub_599() { mpReachStub("_Z16LdrToTransform4fRK20SLdrEditorProperties", "LdrToTransform4f(SLdrEditorProperties const&)"); }

// REL_LoadRiftPortal(CStateManager&, CInputStream&, CEntityInfo&)
extern "C" void reachstub_600() asm("_Z18REL_LoadRiftPortalR13CStateManagerR12CInputStreamR11CEntityInfo");
extern "C" void reachstub_600() { mpReachStub("_Z18REL_LoadRiftPortalR13CStateManagerR12CInputStreamR11CEntityInfo", "REL_LoadRiftPortal(CStateManager&, CInputStream&, CEntityInfo&)"); }

// LdrToEchoParameters(SLdrEchoParameters const&)
extern "C" void reachstub_601() asm("_Z19LdrToEchoParametersRK18SLdrEchoParameters");
extern "C" void reachstub_601() { mpReachStub("_Z19LdrToEchoParametersRK18SLdrEchoParameters", "LdrToEchoParameters(SLdrEchoParameters const&)"); }

// LdrToActorParameters(SLdrActorParameters const&)
extern "C" void reachstub_602() asm("_Z20LdrToActorParametersRK19SLdrActorParameters");
extern "C" void reachstub_602() { mpReachStub("_Z20LdrToActorParametersRK19SLdrActorParameters", "LdrToActorParameters(SLdrActorParameters const&)"); }

// LoadTypedefDamageInfo(SLdrDamageInfo&, CInputStream&)
extern "C" void reachstub_603() asm("_Z21LoadTypedefDamageInfoR14SLdrDamageInfoR12CInputStream");
extern "C" void reachstub_603() { mpReachStub("_Z21LoadTypedefDamageInfoR14SLdrDamageInfoR12CInputStream", "LoadTypedefDamageInfo(SLdrDamageInfo&, CInputStream&)"); }

// LoadTypedefPlayerItem(SLdrPlayerItem&, CInputStream&)
extern "C" void reachstub_604() asm("_Z21LoadTypedefPlayerItemR14SLdrPlayerItemR12CInputStream");
extern "C" void reachstub_604() { mpReachStub("_Z21LoadTypedefPlayerItemR14SLdrPlayerItemR12CInputStream", "LoadTypedefPlayerItem(SLdrPlayerItem&, CInputStream&)"); }

// LoadTypedefAnimationSet(SLdrAnimationSet&, CInputStream&)
extern "C" void reachstub_605() asm("_Z23LoadTypedefAnimationSetR16SLdrAnimationSetR12CInputStream");
extern "C" void reachstub_605() { mpReachStub("_Z23LoadTypedefAnimationSetR16SLdrAnimationSetR12CInputStream", "LoadTypedefAnimationSet(SLdrAnimationSet&, CInputStream&)"); }

// LoadTypedefEchoParameters(SLdrEchoParameters&, CInputStream&)
extern "C" void reachstub_606() asm("_Z25LoadTypedefEchoParametersR18SLdrEchoParametersR12CInputStream");
extern "C" void reachstub_606() { mpReachStub("_Z25LoadTypedefEchoParametersR18SLdrEchoParametersR12CInputStream", "LoadTypedefEchoParameters(SLdrEchoParameters&, CInputStream&)"); }

// LoadTypedefActorParameters(SLdrActorParameters&, CInputStream&)
extern "C" void reachstub_607() asm("_Z26LoadTypedefActorParametersR19SLdrActorParametersR12CInputStream");
extern "C" void reachstub_607() { mpReachStub("_Z26LoadTypedefActorParametersR19SLdrActorParametersR12CInputStream", "LoadTypedefActorParameters(SLdrActorParameters&, CInputStream&)"); }

// LoadTypedefCameraShakerData(SLdrCameraShakerData&, CInputStream&)
extern "C" void reachstub_608() asm("_Z27LoadTypedefCameraShakerDataR20SLdrCameraShakerDataR12CInputStream");
extern "C" void reachstub_608() { mpReachStub("_Z27LoadTypedefCameraShakerDataR20SLdrCameraShakerDataR12CInputStream", "LoadTypedefCameraShakerData(SLdrCameraShakerData&, CInputStream&)"); }

// CGameLight::CGameLight(TUniqueId, TAreaId, bool, rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator> const&, CTransform4f const&, TUniqueId, CLight const&, unsigned int, unsigned int, float, CEntityInfo const*)
extern "C" void reachstub_609() asm("_ZN10CGameLightC1E9TUniqueId7TAreaIdbRKN4rstl12basic_stringIcNS2_11char_traitsIcEENS2_17rmemory_allocatorEEERK12CTransform4fS0_RK6CLightjjfPK11CEntityInfo");
extern "C" void reachstub_609() { mpReachStub("_ZN10CGameLightC1E9TUniqueId7TAreaIdbRKN4rstl12basic_stringIcNS2_11char_traitsIcEENS2_17rmemory_allocatorEEERK12CTransform4fS0_RK6CLightjjfPK11CEntityInfo", "CGameLight::CGameLight(TUniqueId, TAreaId, bool, rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator> const&, CTransform4f const&, TUniqueId, CLight const&, unsigned int, unsigned int, float, CEntityInfo const*)"); }

// CBallCamera::SetState(CBallCamera::EBallCameraState, CStateManager&)
extern "C" void reachstub_610() asm("_ZN11CBallCamera8SetStateENS_16EBallCameraStateER13CStateManager");
extern "C" void reachstub_610() { mpReachStub("_ZN11CBallCamera8SetStateENS_16EBallCameraStateER13CStateManager", "CBallCamera::SetState(CBallCamera::EBallCameraState, CStateManager&)"); }

// CGameCollision::RayStaticLineOfSightTest(CStateManager const&, CVector3f const&, CVector3f const&, float, CMaterialFilter const&)
extern "C" void reachstub_611() asm("_ZN14CGameCollision24RayStaticLineOfSightTestERK13CStateManagerRK9CVector3fS5_fRK15CMaterialFilter");
extern "C" void reachstub_611() { mpReachStub("_ZN14CGameCollision24RayStaticLineOfSightTestERK13CStateManagerRK9CVector3fS5_fRK15CMaterialFilter", "CGameCollision::RayStaticLineOfSightTest(CStateManager const&, CVector3f const&, CVector3f const&, float, CMaterialFilter const&)"); }

// CFluidPlaneManager::CreateSplash(TUniqueId, CStateManager&, CScriptWater const&, CVector3f const&, float, bool)
extern "C" void reachstub_612() asm("_ZN18CFluidPlaneManager12CreateSplashE9TUniqueIdR13CStateManagerRK12CScriptWaterRK9CVector3ffb");
extern "C" void reachstub_612() { mpReachStub("_ZN18CFluidPlaneManager12CreateSplashE9TUniqueIdR13CStateManagerRK12CScriptWaterRK9CVector3ffb", "CFluidPlaneManager::CreateSplash(TUniqueId, CStateManager&, CScriptWater const&, CVector3f const&, float, bool)"); }

// CPlayerKnockBackMgr::DouseFlames__19CPlayerKnockBackMgrFv()
extern "C" void reachstub_613() asm("_ZN19CPlayerKnockBackMgr11fn_801C0124Ev");
extern "C" void reachstub_613() { mpReachStub("_ZN19CPlayerKnockBackMgr11fn_801C0124Ev", "CPlayerKnockBackMgr::DouseFlames__19CPlayerKnockBackMgrFv()"); }

// CGraphics::mModelMatrix
extern "C" __attribute__((aligned(32))) char reachdata_614[0x400] asm("_ZN9CGraphics12mModelMatrixE");
__attribute__((aligned(32))) char reachdata_614[0x400] = {};

// CFluidPlaneManager::GetLastSplashDeltaTime(TUniqueId) const
extern "C" void reachstub_615() asm("_ZNK18CFluidPlaneManager22GetLastSplashDeltaTimeE9TUniqueId");
extern "C" void reachstub_615() { mpReachStub("_ZNK18CFluidPlaneManager22GetLastSplashDeltaTimeE9TUniqueId", "CFluidPlaneManager::GetLastSplashDeltaTime(TUniqueId) const"); }

// __dt__20SLdrEditorPropertiesFv
extern "C" void reachstub_616() asm("__dt__20SLdrEditorPropertiesFv");
extern "C" void reachstub_616() { mpReachStub("__dt__20SLdrEditorPropertiesFv", "__dt__20SLdrEditorPropertiesFv"); }

// fn_802CB918
extern "C" void reachstub_617() asm("fn_802CB918");
extern "C" void reachstub_617() { mpReachStub("fn_802CB918", "fn_802CB918"); }
