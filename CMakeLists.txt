cmake_minimum_required(VERSION 3.25)
project(metroid_prime2_port LANGUAGES C CXX)
if(IOS)
    enable_language(OBJCXX)  # platform/port_ios.mm (disc picker, Documents folder)
endif()

# Native-PC port scaffold for Metroid Prime 2: Echoes (`G2ME01`), a fork of the
# PrimeDecomp/echoes matching decompilation, built the same way as the Metroid
# Prime port: the decompiled game and engine run directly on the GPU through
# Aurora (MIT) instead of an emulator or a static recompilation.
#
# The decompilation is still early (see PORT_NOTES.md), so today this builds the
# game sources against Aurora's SDK headers, plus the port's REL module runtime
# and its tests. `MP_SDK_HEADERS_ONLY` is the only verified configuration.
#
# Reference implementation for every piece of the port layer:
# ../MetroidPrimePort (same engine lineage, complete and working).

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)
include(CTest)

option(MP_SDK_HEADERS_ONLY
    "Compile the decompiled game sources against Aurora's SDK headers without \
linking Aurora. This is the only configuration that works while the Echoes \
decompilation is incomplete, and it is what the shim queue is tracked against."
    ON)

# Android only (see docs/ANDROID.md). Aurora reads the disc through nod, which is a
# Rust crate built by Corrosion - unreachable in the offline cross-build. This option
# substitutes platform/android_probe/nod_stub.c, which implements the stream API
# Aurora's dvd.cpp needs and fails every call. Aurora skips its own nod provider when a
# `nod::nod` target already exists, so the alias below is the whole mechanism.
option(MP_ANDROID_NOD_STUB "Use a non-functional nod stub for Android build probes" OFF)
# As MP1 (ce0bf885e): desktop builds carry the textures/ tree inside the executable; Android's APK
# packs it as assets instead.
if(ANDROID)
    set(_mp_embed_default OFF)
else()
    set(_mp_embed_default ON)
endif()
option(MP_EMBED_RESOURCES "Build the controller glyph and binding icon textures into the executable (desktop)" ${_mp_embed_default})
# As MP1: a system Abseil, fmt, libpng or freetype ties the binary to the build host's copies
# (the macOS runner's Homebrew dylibs, absent on a player's Mac). Bundled, they are built from
# source and linked statically.
if(APPLE)
    set(_mp_bundled_default ON)
else()
    set(_mp_bundled_default OFF)
endif()
option(MP_BUNDLED_DEPS "Build Abseil, fmt, SQLite, libpng and freetype from source and link them statically, ignoring system copies" ${_mp_bundled_default})
set(AURORA_BUNDLED_DEPS ${MP_BUNDLED_DEPS})
if(MP_ANDROID_NOD_STUB)
    if(NOT ANDROID AND NOT WIN32)
        message(FATAL_ERROR "MP_ANDROID_NOD_STUB is only valid for Android and Windows cross-compile probes")
    endif()
    add_library(mp_android_nod_stub STATIC platform/android_probe/nod_stub.c)
    target_include_directories(mp_android_nod_stub PUBLIC platform/android_probe/include)
    add_library(nod::nod ALIAS mp_android_nod_stub)
endif()

# Apple's libc++ still marks std::jthread/std::stop_token (used by aurora's
# threads) experimental; the flag unlocks them and links libc++experimental.
if(APPLE)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-fexperimental-library>)
    # The iOS Simulator SDK ships no libc++experimental; jthread and stop_token
    # are header-only, so its build compiles with the flag and links without it.
    if(NOT CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
        add_link_options(-fexperimental-library)
    endif()
endif()

# MSVC picks a member-function pointer's size per translation unit, from whether
# the class is complete where the pointer type is first needed. ScriptLoaderRel.hpp
# only forward-declares CEntity, so SPlayerActor_FuncPtrs had one layout in
# ScriptLoaderRel.cpp and another in CScriptPlayerActor.cpp, and the first
# cutscene's PlayerActor_TouchModels jumped through garbage. /vmg uses the
# general representation everywhere, as Itanium's fixed one does (MP1 issue #4).
if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/vmg>)
    # cl.exe reports __cplusplus as 199711L unless asked, which sends AUTO to __typeof__.
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>)
endif()

# The REL module tests need modules extracted from a disc the user owns (see
# tools/extract_disc_file.py) plus the symbol list of the executable they were
# linked against. Both are optional: without them the tests report themselves
# skipped. Point MP_REL_FIXTURE at a directory of .rel files to validate the
# whole module set in dependency order.
set(MP_REL_FIXTURE "" CACHE PATH
    "REL module file, or directory of .rel files, extracted from an owned disc")
set(MP_DOL_SYMBOLS "${CMAKE_CURRENT_SOURCE_DIR}/config/G2ME01/symbols.txt" CACHE FILEPATH
    "DOL symbol list matching MP_REL_FIXTURE")

# The decompiled sources are written for a *signed* plain `char`, as PowerPC/MWCC
# and every host the Metroid Prime port has been verified on agree on that. ARM
# defaults `char` to unsigned, which silently changes sentinels such as
# CDSPStreamManager's -1 companion index into 255. Force the verified semantics
# everywhere that compiles game-facing code. MSVC/clang-cl already default to
# signed char.
function(mp_signed_char target)
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -fsigned-char)
    endif()
endfunction()

# Decompiled game + engine sources. The file list is explicit: every translation
# unit that links must be listed, including registration-only ones.
include(files.cmake)

# Aurora provides the GameCube SDK compatibility layer (GX, OS, PAD, DVD, CARD,
# VI, SI, THP, mtx) plus the SDL3/WebGPU application layer. Only its headers are
# used until the decompilation can link.
add_library(mp_game OBJECT ${MP_GAME_SOURCES})
# The same version macros configure.py hands mwcceppc (upstream moved from -DVERSION_G2ME01 to
# -DVERSION=<n> against include/GameVersions.h). Without them `#if VERSION != VERSION_G2ME01`
# only reads right because an undefined macro is 0 in #if.
target_compile_definitions(mp_game PUBLIC BUILD_VERSION=0 VERSION=0)
# The host musyx-port target defines MUSY_TARGET_PC; headers that reach <musyx/musyx.h>
# (CAudioSys.hpp, CAuxEffect.hpp) must agree, or its Dolphin typedefs clash with Aurora's.
target_compile_definitions(mp_game PUBLIC MUSY_TARGET=MUSY_TARGET_PC)

# DIAGNOSTIC ONLY, and off by default. Adds src/MetroidPrime/PortReachStubs.cpp, which
# defines the undefined symbols that the boot path *does* reach, so a diagnostic build can
# run far enough to print which ones it asks for and in what order. Only
# tools/boot_probe.sh passes -DMP_BOOT_STUBS=ON.
#
# **It must never be on for a real build, and the reason is not caution, it is arithmetic:**
# with these stubs the link *succeeds*, so `link_check.sh` would report 0 undefined and be
# wrong by 318. `link_check.sh` never passes this option, which is the only thing keeping the
# honest number honest - so if anything ever adds it there, the gate becomes a liar with no
# symptom. `tools/check_boot_stubs.py` fails if the option is on while the target is the
# real executable.
option(MP_BOOT_STUBS
    "Diagnostic only: stub the REACHABLE undefined symbols so the boot path's order becomes \
observable. NEVER for a real build - it makes the link succeed and lie."
    OFF)
if(MP_BOOT_STUBS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/MetroidPrime/PortReachStubs.cpp")
        message(FATAL_ERROR
            "MP_BOOT_STUBS=ON but src/MetroidPrime/PortReachStubs.cpp is missing. "
            "Generate it with: python3 tools/gen_link_stubs.py --reachable")
    endif()
    message(WARNING
        "MP_BOOT_STUBS=ON: the REACHABLE undefined symbols are being stubbed. This build "
        "is a diagnostic. It is not the port, it must not be run as one, and the undefined "
        "count it reports is 0 by construction.")
    target_sources(mp_game PRIVATE src/MetroidPrime/PortReachStubs.cpp)
endif()

# ---------------------------------------------------------------------------
# MP_PORT_COVERAGE: compiler branch coverage for the port's REL module bodies.
#
# The decomp lanes prove "every arm of a ported body ran" with gdb breakpoints, one spec per
# function (tools/cov). This option replaces that with compiler coverage: the module bodies are
# compiled with gcov, and one ordinary run then reports the branch coverage of every port module
# body at once - no probe, no rebuild per function.
#
# Off by default, and the normal build stays byte-identical: with the option off no flag is added
# and no source is compiled differently (the gate is `ninja -C build-port-link` with the same
# compile lines as before).
#
# Only the module bodies are instrumented: the sources tools/modules_port.txt lists and the
# sibling Port<Class>*.cpp files of the same class. The rest of the game is upstream code, and
# its coverage is not what the lanes ask about. The build goes in its own directory
# (build-port-cov), because a coverage build is not the port.
#
# See docs/LANE_BRIEF.md, "Coverage without probes", and tools/cov/gcov_report.py.
# ---------------------------------------------------------------------------
option(MP_PORT_COVERAGE
    "Instrument the port's REL module bodies with gcov branch coverage. A separate build \
directory; the normal build is unaffected. See docs/LANE_BRIEF.md, 'Coverage without probes'."
    OFF)

if(MP_PORT_COVERAGE)
    # "ModuleName;source path" per line: the port's own module bodies.
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/tools/modules_port.txt" _mp_port_module_lines)
    # The upstream-source modules too (tools/modules_upstream.txt, same format), for the
    # behaviour survey of the modules upstream decompiled (tools/upstream_survey.py).
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/tools/modules_upstream.txt" _mp_upstream_module_lines)
    list(APPEND _mp_port_module_lines ${_mp_upstream_module_lines})
    set(MP_PORT_COVERAGE_SOURCES)
    foreach(_line IN LISTS _mp_port_module_lines)
        string(STRIP "${_line}" _line)
        if(_line STREQUAL "" OR _line MATCHES "^#")
            continue()
        endif()
        string(REGEX REPLACE "^[^;]*;(.*)$" "\\1" _src "${_line}")
        string(STRIP "${_src}" _src)
        if(_src STREQUAL "" OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_src}")
            continue()
        endif()
        list(APPEND MP_PORT_COVERAGE_SOURCES "${_src}")
        # The siblings of the same class: PortCSandworm.cpp brings PortCSandwormStates.cpp and
        # the rest of that class' bodies with it, whether or not tools/modules_port.txt lists
        # them (it lists them today; this keeps the class together if that changes).
        get_filename_component(_dir "${_src}" DIRECTORY)
        get_filename_component(_stem "${_src}" NAME_WE)
        file(GLOB _siblings "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}/${_stem}*.cpp")
        foreach(_sib IN LISTS _siblings)
            file(RELATIVE_PATH _rel "${CMAKE_CURRENT_SOURCE_DIR}" "${_sib}")
            list(APPEND MP_PORT_COVERAGE_SOURCES "${_rel}")
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES MP_PORT_COVERAGE_SOURCES)
    list(LENGTH MP_PORT_COVERAGE_SOURCES _mp_port_cov_count)
    message(STATUS "MP_PORT_COVERAGE=ON: gcov branch coverage on ${_mp_port_cov_count} "
        "module body sources.")
    foreach(_src IN LISTS MP_PORT_COVERAGE_SOURCES)
        # --coverage is the counters; -fprofile-update=atomic makes the counter updates atomic,
        # so a thread that is running when the run ends does not lose (or tear) its counts.
        set_source_files_properties("${_src}" PROPERTIES
            COMPILE_OPTIONS "--coverage;-fprofile-update=atomic")
    endforeach()
endif()

mp_signed_char(mp_game)
# Include order is deliberate: `include/` (the decompilation's own headers) is
# searched *after* Aurora, because Echoes' `include/dolphin/` tree is the partial
# SDK decompilation and must not shadow Aurora's complete, PC-aware SDK view.
# `platform/include/dolphin` fills the SDK headers Aurora omits. `include/LZO`
# is on the path project-wide because the bundled codec is included both as
# `<lzo1x.h>` (upstream's own build does the same).
target_include_directories(mp_game PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/include
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/aurora/include
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/musyx-port/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/LZO)
# compat.h restores the SDK macros Aurora omits; it is force-included ahead of
# every C++ game translation unit. It is C++-only (it pulls in the game's
# CMemory/IAllocator headers), so it must not reach the few C translation units
# in the list - the bundled LZO codec - which are compiled as C, exactly as the
# matching build does.
# clang-cl takes the GNU-style warning flags too; MWCC accepted the narrowing initializers.
set(MP_MSVC_GAME_OPTIONS /W3
    $<$<CXX_COMPILER_ID:Clang>:-Wno-c++11-narrowing -Wno-multichar -Wno-trigraphs>
    # plain cl.exe only: the decomp binds string literals to char* (clang-cl allows it).
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Zc:strictStrings->
    "$<$<COMPILE_LANGUAGE:CXX>:/FI${CMAKE_CURRENT_SOURCE_DIR}/platform/compat.h>")
if(MSVC)
    target_compile_options(mp_game PRIVATE ${MP_MSVC_GAME_OPTIONS})
else()
    # -Werror=return-type is what the Metroid Prime port uses, but the Echoes
    # decompilation is only ~7% done: several not-yet-finished functions fall off
    # the end of a non-void function, exactly as MWCC accepted (undefined
    # behaviour). Inventing return values would be decompilation work, not port
    # work, so the diagnostic stays a warning until upstream completes them; see
    # PORT_NOTES.md. Tighten this back to -Werror=return-type when the
    # decompilation is complete.
    target_compile_options(mp_game PRIVATE
        -Wformat -Wno-error=return-type -Wno-narrowing -Wno-multichar -Wno-write-strings
        # The game's allocator API carries the literal "??(??)" call-stack tags
        # from the PowerPC build; they are trigraphs to the host compiler and
        # are intentional, so keep them out of the log.
        -Wno-trigraphs
        "$<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_SOURCE_DIR}/platform/compat.h>")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
# REL modules. On the console each REL is its own symbol namespace, so their RELMain/RELExit and
# module-local Load* functions collide in a flat host link. Each module is compiled with the game's
# flags as its own object library, then tools/scope_module.py partially links it and makes every
# symbol local except the ones the rest of the program needs (see PORT_NOTES.md).
set(MP_MODULE_OBJECT_TARGETS)
foreach(m ${MP_MODULES})
    add_library(mp_mod_${m} OBJECT ${MP_MODULE_${m}_SOURCES})
    target_compile_definitions(mp_mod_${m} PUBLIC BUILD_VERSION=0 VERSION=0 MUSY_TARGET=MUSY_TARGET_PC)
    mp_signed_char(mp_mod_${m})
    target_include_directories(mp_mod_${m} PUBLIC
        $<TARGET_PROPERTY:mp_game,INTERFACE_INCLUDE_DIRECTORIES>)
    if(NOT MSVC)
        target_compile_options(mp_mod_${m} PRIVATE
            -Wformat -Wno-error=return-type -Wno-narrowing -Wno-multichar -Wno-write-strings
            -Wno-trigraphs
            "$<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_SOURCE_DIR}/platform/compat.h>")
    else()
        target_compile_options(mp_mod_${m} PRIVATE ${MP_MSVC_GAME_OPTIONS})
    endif()
    list(APPEND MP_MODULE_OBJECT_TARGETS mp_mod_${m})
endforeach()
# Compile the modules whenever mp_game is built, so a broken module source is caught by the
# header-only build too.
add_dependencies(mp_game ${MP_MODULE_OBJECT_TARGETS})

if(ANDROID)
    # The module-scoping step partially links every REL module (`ld -r`) and localizes
    # symbols with objcopy/nm, on objects the NDK's clang produced - the host's
    # ld.bfd/objcopy/nm cannot even read an AArch64 object ("file format not
    # recognized"). Use the LLVM tools that ship next to the clang the NDK toolchain
    # selected: `ld.lld -r --force-group-allocation` and `llvm-nm -P --no-demangle` both
    # behave as tools/scope_module.py expects, and llvm-objcopy takes the same
    # --redefine-sym/--localize-symbols arguments.
    get_filename_component(MP_ANDROID_LLVM_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    if(NOT EXISTS "${MP_ANDROID_LLVM_BIN}/ld.lld")
        message(FATAL_ERROR
            "No NDK LLVM toolchain bin directory at ${MP_ANDROID_LLVM_BIN} (from "
            "CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}); the module-scoping tools come "
            "from there.")
    endif()
    find_program(MP_LD_BFD NAMES ld.lld
        HINTS "${MP_ANDROID_LLVM_BIN}" NO_DEFAULT_PATH REQUIRED)
    find_program(MP_OBJCOPY NAMES llvm-objcopy
        HINTS "${MP_ANDROID_LLVM_BIN}" NO_DEFAULT_PATH REQUIRED)
    find_program(MP_NM NAMES llvm-nm
        HINTS "${MP_ANDROID_LLVM_BIN}" NO_DEFAULT_PATH REQUIRED)
elseif(WIN32)
    # COFF has no partial link (lld-link -r); tools/scope_module.py --format coff renames each
    # module's private symbols per object with llvm-objcopy, so only objcopy and nm are needed.
    get_filename_component(MP_WIN_LLVM_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(MP_OBJCOPY NAMES llvm-objcopy ${CMAKE_OBJCOPY} objcopy
        HINTS "${MP_WIN_LLVM_BIN}" REQUIRED)
    find_program(MP_NM NAMES llvm-nm ${CMAKE_NM} nm HINTS "${MP_WIN_LLVM_BIN}" REQUIRED)
    set(MP_LD_BFD "")
elseif(APPLE)
    # Mach-O (macOS, iOS) has no ld.bfd either; tools/scope_module.py --format macho renames
    # per object like the COFF mode. Apple's own objcopy does not exist, so llvm-objcopy and
    # llvm-nm come from Homebrew's llvm (or LLVM_BIN, or the compiler's directory).
    get_filename_component(MP_APPLE_LLVM_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_mp_llvm_hints "${MP_APPLE_LLVM_BIN}" "$ENV{LLVM_BIN}"
        /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin)
    find_program(_mp_brew brew)
    if(_mp_brew)
        execute_process(COMMAND "${_mp_brew}" --prefix llvm OUTPUT_VARIABLE _mp_brew_llvm
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _mp_brew_rc)
        if(_mp_brew_rc EQUAL 0 AND _mp_brew_llvm)
            list(APPEND _mp_llvm_hints "${_mp_brew_llvm}/bin")
        endif()
    endif()
    find_program(MP_OBJCOPY NAMES llvm-objcopy HINTS ${_mp_llvm_hints} REQUIRED)
    find_program(MP_NM NAMES llvm-nm HINTS ${_mp_llvm_hints} REQUIRED)
    set(MP_LD_BFD "")
else()
    find_program(MP_LD_BFD NAMES ld.bfd ld REQUIRED)
    find_program(MP_OBJCOPY NAMES ${CMAKE_OBJCOPY} objcopy REQUIRED)
    find_program(MP_NM NAMES ${CMAKE_NM} nm REQUIRED)
endif()
set(MP_MODULE_DIR "${CMAKE_BINARY_DIR}/modules")
set(MP_MODULE_REGISTRY "${CMAKE_BINARY_DIR}/generated/compiled_modules.inc")

# modules.txt: one "Name;obj;obj" line per module.
set(_mp_modlist "")
foreach(m ${MP_MODULES})
    string(APPEND _mp_modlist "${m};$<JOIN:$<TARGET_OBJECTS:mp_mod_${m}>,;>\n")
endforeach()
file(GENERATE OUTPUT "${MP_MODULE_DIR}/modules.txt" CONTENT "${_mp_modlist}")

# The registry only needs the raw module objects (it names who defines RELMain/RELExit), so it
# can be an input of platform/compiled_modules.cpp without a cycle through the scoping step.
set(_mp_mod_objs)
foreach(m ${MP_MODULES})
    list(APPEND _mp_mod_objs $<TARGET_OBJECTS:mp_mod_${m}>)
endforeach()
add_custom_command(OUTPUT "${MP_MODULE_REGISTRY}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/scope_module.py" registry
            --modules "${MP_MODULE_DIR}/modules.txt" --out "${MP_MODULE_REGISTRY}" --nm "${MP_NM}"
    DEPENDS ${_mp_mod_objs} "${CMAKE_CURRENT_SOURCE_DIR}/tools/scope_module.py"
    COMMENT "Generating compiled-module registry" VERBATIM)
add_custom_target(mp_module_registry DEPENDS "${MP_MODULE_REGISTRY}")

# Called once the other object libraries exist. Produces build/modules/<Name>.o for every module
# and returns the list in MP_SCOPED_MODULE_OBJECTS.
function(mp_scope_modules)
    set(others)
    foreach(t ${ARGN})
        list(APPEND others $<TARGET_OBJECTS:${t}>)
    endforeach()
    set(content "")
    foreach(t ${ARGN})
        string(APPEND content "$<JOIN:$<TARGET_OBJECTS:${t}>,\n>\n")
    endforeach()
    file(GENERATE OUTPUT "${MP_MODULE_DIR}/others.txt" CONTENT "${content}")
    set(outs)
    set(scope_args --ld "${MP_LD_BFD}")
    set(scope_deps)
    if(WIN32 OR APPLE)
        # One scoped object per input object, at paths known now: <Name>/<index>_<source>.obj, in
        # the order $<TARGET_OBJECTS> lists the compiled sources. The tool checks the counts.
        if(WIN32)
            set(scope_format coff)
            set(obj_ext obj)
        else()
            set(scope_format macho)
            set(obj_ext o)
        endif()
        set(outlist "")
        foreach(m ${MP_MODULES})
            set(i 0)
            string(APPEND outlist "${m}")
            foreach(src ${MP_MODULE_${m}_SOURCES})
                if(src MATCHES "\\.(c|cc|cpp|cxx|C)$")
                    get_filename_component(base "${src}" NAME)
                    set(o "${MP_MODULE_DIR}/${m}/${i}_${base}.${obj_ext}")
                    list(APPEND outs "${o}")
                    string(APPEND outlist ";${o}")
                    math(EXPR i "${i} + 1")
                endif()
            endforeach()
            string(APPEND outlist "\n")
        endforeach()
        file(WRITE "${MP_MODULE_DIR}/outputs.txt.tmp" "${outlist}")
        file(COPY_FILE "${MP_MODULE_DIR}/outputs.txt.tmp" "${MP_MODULE_DIR}/outputs.txt" ONLY_IF_DIFFERENT)
        file(REMOVE "${MP_MODULE_DIR}/outputs.txt.tmp")
        set(scope_args --format ${scope_format} --outputs "${MP_MODULE_DIR}/outputs.txt")
        set(scope_deps "${MP_MODULE_DIR}/outputs.txt")
    else()
        foreach(m ${MP_MODULES})
            list(APPEND outs "${MP_MODULE_DIR}/${m}.o")
        endforeach()
    endif()
    add_custom_command(OUTPUT ${outs}
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/scope_module.py" scope
                --modules "${MP_MODULE_DIR}/modules.txt" --others "${MP_MODULE_DIR}/others.txt"
                --outdir "${MP_MODULE_DIR}" ${scope_args} --objcopy "${MP_OBJCOPY}" --nm "${MP_NM}"
        DEPENDS ${_mp_mod_objs} ${others} "${CMAKE_CURRENT_SOURCE_DIR}/tools/scope_module.py"
                ${scope_deps}
        COMMENT "Scoping REL modules" VERBATIM)
    add_custom_target(mp_modules DEPENDS ${outs})
    set(MP_SCOPED_MODULE_OBJECTS ${outs} PARENT_SCOPE)
endfunction()

# The port's own engine-independent sources: SDK shims and stubs, and the
# glibc floor. They compile against Aurora's headers alone, which is what
# keeps them verified while the game cannot link. The engine-facing platform
# files (entry point, debug UI, textures, prompts, randomizer, smoke, disc
# resources) need Echoes' game types and are still Metroid-Prime-shaped; see
# platform/README.md.
add_library(mp_platform OBJECT
    platform/entry.cpp
    platform/port_disc_version.cpp
    platform/compiled_modules.cpp
    platform/rel.cpp
    platform/shims.cpp
    platform/sdk_stubs.cpp
    platform/port_reset.cpp
    platform/port_settings.cpp
    platform/port_collision_view.cpp
    platform/port_mods.cpp
    platform/port_mods_pak.cpp
    platform/port_suit_colours.cpp
    platform/port_suit_colours_disc.cpp
    platform/input/port_input_engine.cpp
    platform/input/port_input_bindings.cpp
    platform/input/port_custom_actions.cpp
    platform/glibc_compat.c)
set_source_files_properties(platform/compiled_modules.cpp PROPERTIES
    OBJECT_DEPENDS "${MP_MODULE_REGISTRY}")
target_include_directories(mp_platform PRIVATE
    "${CMAKE_BINARY_DIR}/generated"
    platform/include
    extern/aurora/include
    extern/musyx-port/include
    include)
target_compile_definitions(mp_platform PRIVATE TARGET_PC AURORA MUSY_TARGET=MUSY_TARGET_PC
    MUSY_VERSION_MAJOR=2 MUSY_VERSION_MINOR=0 MUSY_VERSION_PATCH=3)

# Touch controls: Android draws them in Java (TouchControlsView); iOS draws them in C++
# (platform/debug_ui/touch_overlay.cpp). The desktop switch builds that overlay on a host build so
# it can be tried without a device; the console's `finger` command sends the touches.
option(MP_TOUCH_OVERLAY_DESKTOP "Draw the touch controls on a desktop build (testing)" OFF)
if(ANDROID OR IOS)
    target_compile_definitions(mp_platform PRIVATE PORT_TOUCH_CONTROLS)
endif()
if(IOS)
    target_compile_definitions(mp_platform PRIVATE PORT_TOUCH_OVERLAY)
endif()
if(MP_TOUCH_OVERLAY_DESKTOP AND NOT ANDROID)
    target_compile_definitions(mp_platform PRIVATE PORT_TOUCH_CONTROLS PORT_TOUCH_OVERLAY)
endif()

# The gcov flush hook of a coverage build: a run must leave its .gcda behind however it ends (a
# clean quit, the harness's SIGTERM, a Ctrl-C). It is compiled only into a coverage build, so
# the normal build does not even see the file. See platform/port_coverage.cpp.
if(MP_PORT_COVERAGE)
    target_sources(mp_platform PRIVATE platform/port_coverage.cpp)
    target_compile_definitions(mp_platform PRIVATE MP_PORT_COVERAGE=1)
endif()

# The process entry point. It is compiled but not linked: it builds the OS
# context and memory system the game's entry takes, and those constructors
# are still upstream work (see PORT_NOTES.md).
add_library(mp_port_entry OBJECT platform/main.cpp)
# generated/port_build_info.h: MP_BUILD_REVISION (short git hash, "-dirty" if modified), which
# main.cpp prints at startup. The script rewrites the header only when the text changes.
add_custom_target(port_build_info
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DOUTPUT_HEADER=${CMAKE_BINARY_DIR}/generated/port_build_info.h"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildRevision.cmake"
    BYPRODUCTS "${CMAKE_BINARY_DIR}/generated/port_build_info.h"
    VERBATIM)
add_dependencies(mp_port_entry port_build_info)
target_include_directories(mp_port_entry PRIVATE
    "${CMAKE_BINARY_DIR}/generated"
    platform/include
    extern/aurora/include
    extern/musyx-port/include
    include)
target_compile_definitions(mp_port_entry PRIVATE TARGET_PC AURORA MUSY_TARGET=MUSY_TARGET_PC
    MUSY_VERSION_MAJOR=2 MUSY_VERSION_MINOR=0 MUSY_VERSION_PATCH=3)

# The entry point installs the coverage hook and asks for the shutdown dump (platform/main.cpp).
if(MP_PORT_COVERAGE)
    target_compile_definitions(mp_port_entry PRIVATE MP_PORT_COVERAGE=1)
endif()

if(MP_SDK_HEADERS_ONLY)
    # Aurora sets these on its own targets; the header-only build has to state them.
    target_compile_definitions(mp_game PUBLIC TARGET_PC AURORA)
    foreach(t ${MP_MODULE_OBJECT_TARGETS})
        target_compile_definitions(${t} PUBLIC TARGET_PC AURORA)
    endforeach()
    target_compile_definitions(mp_platform PRIVATE TARGET_PC AURORA MUSY_TARGET=MUSY_TARGET_PC
    MUSY_VERSION_MAJOR=2 MUSY_VERSION_MINOR=0 MUSY_VERSION_PATCH=3)
    target_compile_definitions(mp_port_entry PRIVATE TARGET_PC AURORA MUSY_TARGET=MUSY_TARGET_PC
    MUSY_VERSION_MAJOR=2 MUSY_VERSION_MINOR=0 MUSY_VERSION_PATCH=3)

    mp_scope_modules(mp_game mp_platform mp_port_entry)

    # REL module runtime tests. The runtime is port code (see PORT_NOTES.md), so
    # these need neither Aurora's libraries nor a GPU device.
    set(MP_REL_TEST_SOURCES platform/rel.cpp)

    add_executable(port_rel_tests tests/port_rel_synthetic.cpp ${MP_REL_TEST_SOURCES})
    target_include_directories(port_rel_tests PRIVATE platform/include)
    add_test(NAME port_rel_tests COMMAND port_rel_tests)
    set_tests_properties(port_rel_tests PROPERTIES TIMEOUT 30 LABELS port)

    add_executable(port_rel_real_tests tests/port_rel_real.cpp ${MP_REL_TEST_SOURCES})
    target_include_directories(port_rel_real_tests PRIVATE platform/include)
    if(MP_REL_FIXTURE AND IS_DIRECTORY "${MP_REL_FIXTURE}")
        add_test(NAME port_rel_real_tests
            COMMAND port_rel_real_tests --dir "${MP_REL_FIXTURE}" "${MP_DOL_SYMBOLS}")
    else()
        add_test(NAME port_rel_real_tests
            COMMAND port_rel_real_tests "${MP_REL_FIXTURE}" "${MP_DOL_SYMBOLS}")
    endif()
    set_tests_properties(port_rel_real_tests PROPERTIES
        TIMEOUT 30 LABELS port SKIP_RETURN_CODE 77)

    # Where the REL arena lands on the path Android takes. bionic defines
    # MAP_FIXED_NOREPLACE but not MAP_32BIT, so the fallback is the /proc/self/maps gap
    # scan; MP_ARENA_FORCE_PORTABLE selects that same fallback on a host that does have
    # MAP_32BIT (see the comment on the hook in platform/rel.cpp), which is the only way
    # to run Android's placement logic without a phone. The other test targets must not
    # carry the define: they cover the MAP_32BIT path.
    add_executable(port_arena_placement_tests tests/port_arena_placement.cpp platform/rel.cpp)
    target_include_directories(port_arena_placement_tests PRIVATE platform/include)
    target_compile_definitions(port_arena_placement_tests PRIVATE MP_ARENA_FORCE_PORTABLE)
    add_test(NAME port_arena_placement_tests COMMAND port_arena_placement_tests)
    set_tests_properties(port_arena_placement_tests PROPERTIES TIMEOUT 30 LABELS port)

    add_executable(port_save_trilogy_tests tests/port_save_trilogy.cpp platform/port_save_trilogy.cpp
        platform/port_save_trilogy_payload.cpp)
    target_include_directories(port_save_trilogy_tests PRIVATE platform/include)
    # The fixtures are real saves (untracked: they carry the game's banner and icon); the test
    # only runs where a copy sits in tests/fixtures/trilogy.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/trilogy/01-firstboot.bin)
        add_test(NAME port_save_trilogy_tests
            COMMAND port_save_trilogy_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/trilogy)
        set_tests_properties(port_save_trilogy_tests PROPERTIES TIMEOUT 30 LABELS port)
    endif()

    add_executable(port_rando_models_tests tests/port_rando_models.cpp platform/port_rando_models.cpp)
    target_include_directories(port_rando_models_tests PRIVATE platform/include)
    add_test(NAME port_rando_models_tests COMMAND port_rando_models_tests)
    set_tests_properties(port_rando_models_tests PROPERTIES TIMEOUT 30 LABELS port)

    add_executable(port_rando_world_tests tests/port_rando_world.cpp platform/port_rando_world.cpp)
    target_include_directories(port_rando_world_tests PRIVATE platform/include)
    add_test(NAME port_rando_world_tests COMMAND port_rando_world_tests)
    set_tests_properties(port_rando_world_tests PROPERTIES TIMEOUT 30 LABELS port)

    add_executable(port_rando_logic_tests tests/port_rando_logic.cpp platform/port_rando_logic.cpp
        platform/port_rando_world.cpp)
    target_include_directories(port_rando_logic_tests PRIVATE platform/include)
    add_test(NAME port_rando_logic_tests COMMAND port_rando_logic_tests)
    set_tests_properties(port_rando_logic_tests PROPERTIES TIMEOUT 30 LABELS port)

    add_executable(port_rando_gen_tests tests/port_rando_gen.cpp platform/port_rando_gen.cpp
        platform/port_rando_logic.cpp platform/port_rando_world.cpp)
    target_include_directories(port_rando_gen_tests PRIVATE platform/include)
    add_test(NAME port_rando_gen_tests COMMAND port_rando_gen_tests)
    set_tests_properties(port_rando_gen_tests PROPERTIES TIMEOUT 120 LABELS port)

    add_executable(port_entry_tests tests/port_entry_tests.cpp platform/entry.cpp)
    target_include_directories(port_entry_tests PRIVATE platform/include extern/aurora/include)
    target_compile_definitions(port_entry_tests PRIVATE TARGET_PC)
    add_test(NAME port_entry_tests COMMAND port_entry_tests)
    set_tests_properties(port_entry_tests PROPERTIES TIMEOUT 30 LABELS port)

    message(STATUS
        "MP_SDK_HEADERS_ONLY=ON: building the game sources against Aurora's headers "
        "only (no Aurora, no executable). Set -DMP_SDK_HEADERS_ONLY=OFF once the "
        "decompilation is complete enough to link. See PORT_NOTES.md.")
    return()
endif()

add_subdirectory(extern/aurora EXCLUDE_FROM_ALL)
if(TARGET aurora_mtx)
    target_compile_definitions(aurora_mtx PRIVATE MTX_USE_PS=1)
endif()

# platform/ai_dma.cpp is the GameCube AI DMA path on the host, and it is the port's
# implementation of all five AI DMA entry points (Aurora's dolphin/ai.h declares
# them and implements none). It is a separate target rather than part of mp_platform
# because it is the one port source that is not engine-independent: it includes
# SDL3, which only exists once Aurora has been added below and fetched it. That
# also keeps tools/probe_sources.sh honest - it covers mp_platform and
# mp_port_entry, which compile against Aurora's headers alone, and this file is
# syntax-checked by the full build instead.
# OpenSSL gives the HTTPS client (platform/port_ws.cpp) its TLS, for the update check. On the desktop
# it is optional: without it the port still builds and the check fails with a clear error. On Android
# it is built from source (cmake/AndroidOpenSSL.cmake), because the NDK has no TLS library and a
# find_package that quietly finds nothing is how an APK ends up unable to reach GitHub. Leaving it
# out there has to be asked for. Mirrors the Prime 1 port's CMakeLists.txt.
option(MP_ALLOW_NO_TLS "Android: build without OpenSSL, so the update check's https:// is refused" OFF)
if(ANDROID)
    if(NOT MP_ALLOW_NO_TLS)
        include(cmake/AndroidOpenSSL.cmake)
    endif()
else()
    find_package(OpenSSL)
endif()
function(mp_websocket_tls target)
    if(OpenSSL_FOUND)
        target_compile_definitions(${target} PRIVATE MP_HAVE_OPENSSL)
        target_link_libraries(${target} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        if(WIN32)
            target_link_libraries(${target} PRIVATE crypt32)
        endif()
        if(MP_OPENSSL_BUILD_TARGET)
            add_dependencies(${target} ${MP_OPENSSL_BUILD_TARGET})
        endif()
    endif()
endfunction()
if(OpenSSL_FOUND)
    message(STATUS "OpenSSL ${OPENSSL_VERSION} found: https:// enabled")
else()
    message(STATUS "OpenSSL not found: https:// disabled")
endif()

if(AURORA_SDL3_TARGET)
    add_library(mp_port_audio OBJECT platform/ai_dma.cpp platform/port_controls.cpp
        platform/port_save.cpp platform/port_save_convert.cpp platform/port_save_trilogy.cpp platform/port_save_trilogy_payload.cpp platform/port_savestate.cpp platform/port_beam_ammo.cpp platform/port_savestate_room.cpp platform/port_savestate_format.cpp platform/port_randomizer.cpp platform/port_rando_gen.cpp platform/port_rando_models.cpp platform/port_rando_world.cpp platform/port_rando_logic.cpp platform/port_logfile.cpp platform/port_breadcrumb.cpp
        platform/port_crash.cpp platform/port_watchdog.cpp platform/port_memory_log.cpp platform/port_storage_report.cpp
        platform/port_pointer.cpp platform/port_pointer_menu.cpp
        platform/port_events.cpp platform/port_livesplit.cpp platform/port_harness.cpp platform/port_mouse.cpp
        platform/port_radial.cpp
        platform/port_warp.cpp platform/port_freecam.cpp platform/port_photo.cpp platform/port_console.cpp platform/port_overlay.cpp platform/port_disc_replace.cpp
        platform/port_gyro_motion.cpp
        platform/port_dynamic_res.cpp platform/port_video.cpp platform/port_gpu_driver.cpp
        platform/port_data_folder.cpp
        platform/port_json.cpp platform/port_ws.cpp platform/port_update_check.cpp
        platform/debug_ui/audio.cpp platform/debug_ui/card_data.cpp platform/debug_ui/cheats.cpp platform/debug_ui/controls.cpp platform/debug_ui/debug.cpp
        platform/debug_ui/game_sections.cpp platform/debug_ui/mods.cpp platform/debug_ui/performance.cpp platform/debug_ui/randomizer.cpp
        platform/debug_ui/savestates.cpp platform/debug_ui/search.cpp platform/debug_ui/settings.cpp
        platform/debug_ui/photo.cpp platform/debug_ui/tabs.cpp platform/debug_ui/ui_common.cpp platform/debug_ui/video.cpp
        platform/debug_ui/window.cpp platform/debug_ui/touch_overlay.cpp
        platform/port_touch.cpp platform/port_touch_jni.cpp platform/touch_pad.cpp
        platform/input/port_input_remap_ui.cpp
        platform/input/port_input_devices.cpp
        platform/input/port_input_user_presets.cpp platform/input/port_input_user_presets_store.cpp
        platform/port_embedded.cpp platform/port_textures.cpp platform/port_zip.cpp platform/port_prompts.cpp
        platform/port_discord.cpp
        platform/port_disc_verify.cpp
        platform/port_disc_verify_core.cpp)
    # port_crash.cpp prints MP_BUILD_REVISION in its report.
    add_dependencies(mp_port_audio port_build_info)
    target_include_directories(mp_port_audio PRIVATE
        "${CMAKE_BINARY_DIR}/generated"
        platform/include
        extern/aurora/include
        extern/musyx-port/include
        include
        $<TARGET_PROPERTY:${AURORA_SDL3_TARGET},INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(mp_port_audio PRIVATE TARGET_PC AURORA
        MUSY_TARGET=MUSY_TARGET_PC)
    if(MP_PORT_COVERAGE)
        # port_console.cpp's `covdump` (platform/port_coverage.cpp).
        target_compile_definitions(mp_port_audio PRIVATE MP_PORT_COVERAGE=1)
    endif()
    # The F1 overlay and the touch overlay (platform/debug_ui) are built here, not in mp_platform.
    if(ANDROID OR IOS OR MP_TOUCH_OVERLAY_DESKTOP)
        target_compile_definitions(mp_port_audio PRIVATE PORT_TOUCH_CONTROLS)
    endif()
    if(IOS OR (MP_TOUCH_OVERLAY_DESKTOP AND NOT ANDROID))
        target_compile_definitions(mp_port_audio PRIVATE PORT_TOUCH_OVERLAY)
    endif()
    mp_signed_char(mp_port_audio)
    mp_websocket_tls(mp_port_audio)
    # aurora::core carries ImGui's include directory, which the F1 overlay builds against.
    target_link_libraries(mp_port_audio PRIVATE ${AURORA_SDL3_TARGET} aurora::core)
    # port_zip.cpp includes <zlib.h>: Aurora provides ZLIB::ZLIB (system or fetched zlib-ng),
    # and Windows has no system header to fall back on.
    if(TARGET ZLIB::ZLIB)
        target_link_libraries(mp_port_audio PRIVATE ZLIB::ZLIB)
    endif()
    # platform/main.cpp turns fd 1 and fd 2 into logcat lines and resolves its directories
    # through SDL_GetPrefPath on Android, so the entry object needs SDL3's headers there.
    # AURORA_SDL3_TARGET only exists once Aurora has been added, which is why this is here
    # rather than with the target itself; the desktop path needs it for the disc message and
    # picker.
    target_include_directories(mp_port_entry PRIVATE
        $<TARGET_PROPERTY:${AURORA_SDL3_TARGET},INTERFACE_INCLUDE_DIRECTORIES>)
else()
    message(FATAL_ERROR
        "platform/ai_dma.cpp needs SDL3, but Aurora provided no AURORA_SDL3_TARGET. "
        "The game links AIInitDMA/AIStartDMA/AIGetDMAStartAddr/AIRegisterDMACallback "
        "from it, so without SDL the port cannot link.")
endif()

# MusyX is the game's audio engine; the vendored fork is portable. Echoes uses 2.0.3 (as in
# configure.py): its group-stack API (sndStack*) and per-stack ARAM only exist from 2.0.2.
set(MUSYX_VERSION_PATCH 3 CACHE STRING "" FORCE)
add_subdirectory(extern/musyx-port EXCLUDE_FROM_ALL)
mp_signed_char(musyx)

target_link_libraries(mp_game PUBLIC
    aurora::core aurora::gx aurora::gd aurora::si aurora::vi aurora::pad
    aurora::mtx aurora::os aurora::dvd aurora::thp aurora::card
    musyx)
foreach(t ${MP_MODULE_OBJECT_TARGETS})
    target_link_libraries(${t} PRIVATE
        aurora::core aurora::gx aurora::gd aurora::si aurora::vi aurora::pad
        aurora::mtx aurora::os aurora::dvd aurora::thp aurora::card musyx)
endforeach()

# The touch overlay's virtual gamepad, on the host. `platform/touch_pad.cpp` is platform
# independent by construction (that is the whole point of the file), so its axis conversion
# and button mapping can be checked without a phone - and a wrong mapping is otherwise only
# visible on one, which is where nobody runs tests. MP1 has the same target, and like MP1's
# it is not behind BUILD_TESTING: some dependency of Aurora forces that variable to OFF
# internally (build-port-link/CMakeCache.txt), so a condition on it would silently drop the
# test.
if(AURORA_SDL3_TARGET)
    enable_testing()
    # Every F1 row is in platform/port_settings_index.inc (the settings search) and every settings key
    # in docs/SETTINGS.md (tools/check_settings_index.py, MP1's checker).
    add_test(NAME port_settings_index COMMAND ${Python3_EXECUTABLE} -I
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_settings_index.py ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(port_settings_index PROPERTIES TIMEOUT 30 LABELS port)
    # tools/scope_module.py --format macho on arm64-apple-macos objects; skipped (77) when the
    # llvm-mingw clang the test borrows is absent.
    add_test(NAME port_scope_macho
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/port_scope_macho_test.py)
    set_tests_properties(port_scope_macho PROPERTIES TIMEOUT 60 LABELS port SKIP_RETURN_CODE 77)
    # Every command in the console's table is documented in docs/CONSOLE.md.
    add_test(NAME port_console_docs
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/port_console_docs.py
                ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(port_console_docs PROPERTIES TIMEOUT 15 LABELS port)
    # Every settings.cfg key reads back as written (platform/debug_ui/settings.cpp, kSettingKeys); the game
    # binary checks itself and exits before starting.
    add_test(NAME port_settings_roundtrip COMMAND metroid_prime2_port --settings-selftest)
    set_tests_properties(port_settings_roundtrip PROPERTIES TIMEOUT 30 LABELS port
        ENVIRONMENT "MP2_USER_PATH=${CMAKE_CURRENT_BINARY_DIR}/settings-selftest/")
    # F1 > System > Settings > Reset configuration (platform/include/port_config_reset.h). Header-only.
    add_executable(port_radial_tests tests/port_radial.cpp platform/port_radial.cpp)
    target_include_directories(port_radial_tests PRIVATE platform/include)
    add_test(NAME port_radial_tests COMMAND port_radial_tests)
    add_executable(port_radial_retry_tests tests/port_radial_retry.cpp platform/port_radial.cpp)
    target_include_directories(port_radial_retry_tests PRIVATE platform/include)
    add_test(NAME port_radial_retry_tests COMMAND port_radial_retry_tests)
    add_executable(port_radial_latch_tests tests/port_radial_latch.cpp platform/port_radial.cpp)
    target_include_directories(port_radial_latch_tests PRIVATE platform/include)
    add_test(NAME port_radial_latch_tests COMMAND port_radial_latch_tests)
    add_executable(port_radial_touch_tests tests/port_radial_touch.cpp platform/port_radial.cpp)
    find_package(Threads REQUIRED)
    target_include_directories(port_radial_touch_tests PRIVATE platform/include)
    target_link_libraries(port_radial_touch_tests PRIVATE Threads::Threads)
    add_test(NAME port_radial_touch_tests COMMAND port_radial_touch_tests)
    add_executable(port_radial_mouse_tests tests/port_mouse.cpp platform/port_mouse.cpp platform/port_radial.cpp)
    target_include_directories(port_radial_mouse_tests PRIVATE platform/include extern/aurora/include)
    target_link_libraries(port_radial_mouse_tests PRIVATE ${AURORA_SDL3_TARGET})
    add_test(NAME port_radial_mouse_tests COMMAND port_radial_mouse_tests)
    set_tests_properties(port_radial_tests port_radial_retry_tests port_radial_touch_tests port_radial_mouse_tests
        PROPERTIES TIMEOUT 30 LABELS port)
    add_executable(port_save_framing_tests tests/port_save_framing.cpp src/Kyoto/CCrc32.cpp)
    target_include_directories(port_save_framing_tests PRIVATE platform/include include tests)
    target_compile_options(port_save_framing_tests PRIVATE $<IF:$<BOOL:${MSVC}>,/FIcstdint,-include$<SEMICOLON>cstdint>)
    add_test(NAME port_save_framing_tests COMMAND port_save_framing_tests)
    set_tests_properties(port_save_framing_tests PROPERTIES TIMEOUT 30 LABELS port)

    add_executable(port_save_mirror_tests tests/port_save_mirror.cpp)
    target_include_directories(port_save_mirror_tests PRIVATE platform/include tests)
    add_test(NAME port_save_mirror_tests COMMAND port_save_mirror_tests)
    set_tests_properties(port_save_mirror_tests PROPERTIES TIMEOUT 30 LABELS port)

    # G2ME <-> G2MP save conversion round trip on tests/data/01-G2ME-MetroidPrime2.gci (a real
    # save, untracked like the Trilogy fixtures: registered only where a copy is present).
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/01-G2ME-MetroidPrime2.gci)
        add_executable(port_save_convert_tests tests/port_save_convert.cpp platform/port_save_convert.cpp
            src/Kyoto/CCrc32.cpp)
        target_include_directories(port_save_convert_tests PRIVATE platform/include include tests)
        target_compile_definitions(port_save_convert_tests PRIVATE
            SAVE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/01-G2ME-MetroidPrime2.gci")
        target_compile_options(port_save_convert_tests PRIVATE $<IF:$<BOOL:${MSVC}>,/FIcstdint,-include$<SEMICOLON>cstdint>)
        add_test(NAME port_save_convert_tests COMMAND port_save_convert_tests)
        set_tests_properties(port_save_convert_tests PROPERTIES TIMEOUT 30 LABELS port)
    endif()
    # Android's data folder move: the copy engine and the port_paths.txt rewrite (platform/port_data_folder.cpp).
    add_executable(port_data_folder_tests tests/port_data_folder.cpp platform/port_data_folder.cpp)
    target_include_directories(port_data_folder_tests PRIVATE platform/include tests)
    add_test(NAME port_data_folder_tests COMMAND port_data_folder_tests
        ${CMAKE_CURRENT_BINARY_DIR}/data-folder-test)
    set_tests_properties(port_data_folder_tests PROPERTIES TIMEOUT 30 LABELS port)
    add_executable(port_mods_tests tests/port_mods.cpp platform/port_mods_pak.cpp)
    target_include_directories(port_mods_tests PRIVATE platform/include)
    add_test(NAME port_mods_tests COMMAND port_mods_tests)
    set_tests_properties(port_mods_tests PROPERTIES TIMEOUT 15 LABELS port)
    add_executable(port_mods_disc_tests tests/port_mods_disc.cpp)
    target_include_directories(port_mods_disc_tests PRIVATE platform/include extern/aurora/include)
    target_compile_definitions(port_mods_disc_tests PRIVATE TARGET_PC)
    add_test(NAME port_mods_disc_tests COMMAND port_mods_disc_tests)
    set_tests_properties(port_mods_disc_tests PROPERTIES TIMEOUT 15 LABELS port)
    add_executable(port_suit_colours_tests tests/port_suit_colours.cpp platform/port_suit_colours.cpp)
    target_include_directories(port_suit_colours_tests PRIVATE platform/include)
    add_test(NAME port_suit_colours_tests COMMAND port_suit_colours_tests)
    set_tests_properties(port_suit_colours_tests PROPERTIES TIMEOUT 15 LABELS port)
    # The texture-pack zip reader (platform/port_zip.cpp) against archives the test builds.
    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
        add_executable(port_zip_tests tests/port_zip.cpp platform/port_zip.cpp)
        target_include_directories(port_zip_tests PRIVATE platform/include tests)
        target_link_libraries(port_zip_tests PRIVATE ZLIB::ZLIB)
        add_test(NAME port_zip_tests COMMAND port_zip_tests)
        set_tests_properties(port_zip_tests PROPERTIES TIMEOUT 30 LABELS port)
    endif()

    add_executable(port_config_reset_tests tests/port_config_reset.cpp)
    target_include_directories(port_config_reset_tests PRIVATE platform/include)
    add_test(NAME port_config_reset_tests COMMAND port_config_reset_tests
        "${CMAKE_CURRENT_BINARY_DIR}/config-reset-test")
    set_tests_properties(port_config_reset_tests PROPERTIES TIMEOUT 30 LABELS port)
    add_executable(port_touch_pad_tests tests/touch_pad.cpp platform/touch_pad.cpp)
    target_include_directories(port_touch_pad_tests PRIVATE platform/include extern/aurora/include
        $<TARGET_PROPERTY:${AURORA_SDL3_TARGET},INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(port_touch_pad_tests PRIVATE TARGET_PC)
    target_link_libraries(port_touch_pad_tests PRIVATE ${AURORA_SDL3_TARGET})
    add_test(NAME port_touch_pad_tests COMMAND port_touch_pad_tests)
    set_tests_properties(port_touch_pad_tests PROPERTIES TIMEOUT 30 LABELS port)
    # Header-only (port_hold_toggle.h): no Aurora link, no TARGET_PC.
    add_executable(port_hold_toggle_tests tests/port_hold_toggle.cpp)
    target_include_directories(port_hold_toggle_tests PRIVATE platform/include)
    add_test(NAME port_hold_toggle_tests COMMAND port_hold_toggle_tests)
    set_tests_properties(port_hold_toggle_tests PROPERTIES TIMEOUT 30 LABELS port)

    # Header-only (port_input_pad.h): the MP2_INPUT `pN.` prefix parser.
    add_executable(port_input_pad_tests tests/port_input_pad.cpp)
    target_include_directories(port_input_pad_tests PRIVATE platform/include)
    add_test(NAME port_input_pad_tests COMMAND port_input_pad_tests)
    set_tests_properties(port_input_pad_tests PROPERTIES TIMEOUT 30 LABELS port)
    # Header-only (port_input_gate.h): pad 0's neutral-while-F1-is-open gate.
    add_executable(port_input_gate_tests tests/port_input_gate.cpp)
    target_include_directories(port_input_gate_tests PRIVATE platform/include)
    add_test(NAME port_input_gate_tests COMMAND port_input_gate_tests)
    set_tests_properties(port_input_gate_tests PROPERTIES TIMEOUT 30 LABELS port)
    # Quick resume's pure rules (platform/include/port_resume_logic.h). Header-only.
    add_executable(port_resume_logic_tests tests/port_resume_logic.cpp)
    target_include_directories(port_resume_logic_tests PRIVATE platform/include)
    add_test(NAME port_resume_logic_tests COMMAND port_resume_logic_tests)
    set_tests_properties(port_resume_logic_tests PROPERTIES TIMEOUT 30 LABELS port)
    # Pointer menus' pure parts (port_pointer_math.h, port_pointer_gesture.h): letterbox mapping,
    # projection rects, the gesture recogniser. Header-only.
    add_executable(port_pointer_tests tests/port_pointer.cpp)
    target_include_directories(port_pointer_tests PRIVATE platform/include)
    add_test(NAME port_pointer_tests COMMAND port_pointer_tests)
    set_tests_properties(port_pointer_tests PROPERTIES TIMEOUT 30 LABELS port)
    add_executable(port_game_text_tests tests/port_game_text.cpp)
    target_include_directories(port_game_text_tests PRIVATE platform/include)
    add_test(NAME port_game_text_tests COMMAND port_game_text_tests)
    set_tests_properties(port_game_text_tests PROPERTIES TIMEOUT 30 LABELS port)
    # The presentation rules (platform/include/port_presentation_rules.h): snapshot freshness, the
    # actor blend's snap rules and the morph ball's pivoted blend. Header-only.
    add_executable(port_presentation_rules_tests tests/port_presentation_rules.cpp)
    target_include_directories(port_presentation_rules_tests PRIVATE platform/include)
    add_test(NAME port_presentation_rules_tests COMMAND port_presentation_rules_tests)
    set_tests_properties(port_presentation_rules_tests PROPERTIES TIMEOUT 30 LABELS port)
    # The tick pair (platform/include/port_tick_pair.h) that swooshes, electric effects and beams
    # blend between ticks with. Header-only.
    add_executable(port_tick_pair_tests tests/port_tick_pair.cpp)
    target_include_directories(port_tick_pair_tests PRIVATE platform/include)
    add_test(NAME port_tick_pair_tests COMMAND port_tick_pair_tests)
    set_tests_properties(port_tick_pair_tests PROPERTIES TIMEOUT 30 LABELS port)
    # Dynamic resolution's controller (platform/port_dynamic_res.cpp, MP1's) on modelled scenes.
    add_executable(port_dynamic_res_tests tests/port_dynamic_res.cpp platform/port_dynamic_res.cpp)
    target_include_directories(port_dynamic_res_tests PRIVATE platform/include)
    add_test(NAME port_dynamic_res_tests COMMAND port_dynamic_res_tests)
    set_tests_properties(port_dynamic_res_tests PROPERTIES TIMEOUT 30 LABELS port)
    add_executable(port_tri_orbit_tests tests/port_tri_orbit.cpp)
    target_include_directories(port_tri_orbit_tests PRIVATE platform/include)
    add_test(NAME port_tri_orbit_tests COMMAND port_tri_orbit_tests)
    set_tests_properties(port_tri_orbit_tests PROPERTIES TIMEOUT 30 LABELS port)
    add_executable(port_tri_reticle_tests tests/port_tri_reticle.cpp)
    target_include_directories(port_tri_reticle_tests PRIVATE platform/include)
    add_test(NAME port_tri_reticle_tests COMMAND port_tri_reticle_tests)
    set_tests_properties(port_tri_reticle_tests PROPERTIES TIMEOUT 30 LABELS port)
    # The renderer picker's pure logic (platform/include/port_graphics_backend.h).
    add_executable(port_graphics_backend_tests tests/port_graphics_backend.cpp)
    target_include_directories(port_graphics_backend_tests PRIVATE platform/include extern/aurora/include)
    add_test(NAME port_graphics_backend_tests COMMAND port_graphics_backend_tests)
    set_tests_properties(port_graphics_backend_tests PROPERTIES TIMEOUT 30 LABELS port)
    # The input engine (platform/input/port_input_engine.cpp): bindings, chords, contexts and
    # device profiles resolve to the right pad state. Engine-independent: no SDL, Aurora or game.
    add_executable(port_input_engine_tests tests/port_input_engine.cpp
        platform/input/port_input_engine.cpp)
    target_include_directories(port_input_engine_tests PRIVATE platform/include)
    add_test(NAME port_input_engine_tests COMMAND port_input_engine_tests)
    set_tests_properties(port_input_engine_tests PROPERTIES TIMEOUT 30 LABELS port)
    add_executable(port_controller_regressions tests/port_controller_regressions.cpp
        platform/input/port_input_engine.cpp platform/input/port_input_bindings.cpp)
    target_include_directories(port_controller_regressions PRIVATE platform/include extern/aurora/include)
    target_compile_definitions(port_controller_regressions PRIVATE TARGET_PC)
    target_link_libraries(port_controller_regressions PRIVATE ${AURORA_SDL3_TARGET})
    add_test(NAME port_controller_regressions COMMAND port_controller_regressions)
    set_tests_properties(port_controller_regressions PROPERTIES TIMEOUT 30 LABELS port)
    # The gyro filter (platform/port_gyro_motion.cpp, MP1's): calibration, manual calibration,
    # spaces, smoothing and tightening over GamepadMotionHelpers. No SDL, Aurora or game.
    add_executable(port_gyro_motion_tests tests/port_gyro_motion.cpp platform/port_gyro_motion.cpp)
    target_include_directories(port_gyro_motion_tests PRIVATE platform/include)
    add_test(NAME port_gyro_motion_tests COMMAND port_gyro_motion_tests)
    set_tests_properties(port_gyro_motion_tests PROPERTIES TIMEOUT 30 LABELS port)
    # The bindings parser/writer (platform/input/port_input_bindings.cpp): a controls file parses
    # into engine bindings and round-trips, with bad input rejected rather than half-applied.
    add_executable(port_input_bindings_tests tests/port_input_bindings.cpp
        platform/input/port_input_bindings.cpp platform/input/port_input_engine.cpp)
    target_include_directories(port_input_bindings_tests PRIVATE platform/include)
    add_test(NAME port_input_bindings_tests COMMAND port_input_bindings_tests)
    set_tests_properties(port_input_bindings_tests PROPERTIES TIMEOUT 30 LABELS port)

    # Saved binding presets (platform/input/port_input_user_presets.cpp): the file format round-trips.
    add_executable(port_input_user_presets_tests tests/port_input_user_presets.cpp
        platform/input/port_input_user_presets.cpp
        platform/input/port_input_bindings.cpp platform/input/port_input_engine.cpp)
    target_include_directories(port_input_user_presets_tests PRIVATE platform/include)
    add_test(NAME port_input_user_presets_tests COMMAND port_input_user_presets_tests)
    set_tests_properties(port_input_user_presets_tests PROPERTIES TIMEOUT 30 LABELS port)
endif()

# ---------------------------------------------------------------------------
# The binary. `mp_port_entry` is platform/main.cpp, which is the process entry:
# it brings Aurora up (which is what creates the window), mounts the disc, and calls
# InvokeCMain - see PORT_NOTES.md. So the Echoes binary is those three object
# libraries and Aurora, in the same shape ../MetroidPrimePort uses.
#
# On Android it is a *shared library*, not an executable: SDL's Java activity loads
# `libmetroid_prime2_port.so` and calls `SDL_main` in it. `-Wl,-u,SDL_main` keeps the
# entry point, which nothing in the library references, from being dropped.
# ---------------------------------------------------------------------------
mp_scope_modules(mp_game mp_platform mp_port_entry mp_port_audio)
if(ANDROID)
    add_library(metroid_prime2_port SHARED)
elseif(IOS)
    # A flat .app: the executable, Info.plist and the textures folder in one directory.
    # tools/ios_package.sh adds the icons, licences and signature (docs/IOS.md).
    file(STRINGS android/app/build.gradle _mp_version_line REGEX "versionName \"[^\"]+\"" LIMIT_COUNT 1)
    string(REGEX REPLACE ".*versionName \"([^\"]+)\".*" "\\1" MP_APP_VERSION "${_mp_version_line}")
    add_executable(metroid_prime2_port MACOSX_BUNDLE platform/port_ios.mm)
    set_source_files_properties(platform/port_ios.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_include_directories(metroid_prime2_port PRIVATE platform/include
        $<TARGET_PROPERTY:${AURORA_SDL3_TARGET},INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(metroid_prime2_port PRIVATE
        ${AURORA_SDL3_TARGET} "-framework UIKit" "-framework UniformTypeIdentifiers")
    set_target_properties(metroid_prime2_port PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/ios/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.odrannnn.metroidprime2port"
        MACOSX_BUNDLE_BUNDLE_NAME "Metroid Prime 2 Port"
        MACOSX_BUNDLE_EXECUTABLE_NAME "metroid_prime2_port"
        MACOSX_BUNDLE_BUNDLE_VERSION "${MP_APP_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${MP_APP_VERSION}"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${MP_APP_VERSION}")
else()
    add_executable(metroid_prime2_port)
endif()
if(WIN32)
    # A GUI program, so Windows opens no console window next to the game (SDL_main supplies
    # WinMain). main() attaches to the parent terminal's console, if any.
    set_target_properties(metroid_prime2_port PROPERTIES WIN32_EXECUTABLE ON)
    # shell32/ole32/advapi32: finding Dolphin's folder (port_save.cpp).
    # dbghelp: the crash report's stack and dump (port_crash.cpp).
    target_link_libraries(metroid_prime2_port PRIVATE shell32 ole32 advapi32 dbghelp)
    # UTF-8 as the ANSI code page, so the UTF-8 paths SDL returns work with the narrow file APIs.
    target_sources(metroid_prime2_port PRIVATE packaging/metroid_prime2_port.manifest)
    # The icon Explorer and the window show.
    target_sources(metroid_prime2_port PRIVATE packaging/metroid_prime2_port.rc)
    # The DLLs the exe imports (Dawn's) go next to it, so ctest can start it from the build tree.
    include(extern/aurora/cmake/AuroraCopyRuntimeDLLs.cmake)
    aurora_copy_runtime_dlls(metroid_prime2_port)
endif()
set_source_files_properties(${MP_SCOPED_MODULE_OBJECTS} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
target_sources(metroid_prime2_port PRIVATE
    ${MP_SCOPED_MODULE_OBJECTS}
    $<TARGET_OBJECTS:mp_port_entry>
    $<TARGET_OBJECTS:mp_platform>
    $<TARGET_OBJECTS:mp_port_audio>)
target_include_directories(metroid_prime2_port PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(metroid_prime2_port PRIVATE TARGET_PC AURORA)
mp_signed_char(metroid_prime2_port)
mp_websocket_tls(metroid_prime2_port)
if(ANDROID AND OpenSSL_FOUND)
    # The OpenSSL linked in is private to the game library. Exporting its symbols would let them
    # interpose on, or be interposed by, another copy in the process. Only its archives are named:
    # excluding ALL would also hide the JNI entry points in the static SDL archive.
    target_link_options(metroid_prime2_port PRIVATE "-Wl,--exclude-libs,libssl.a:libcrypto.a")
endif()

# A coverage build links libgcov: the counters' writer, and __gcov_dump() for the flush hook.
if(MP_PORT_COVERAGE)
    target_link_options(metroid_prime2_port PRIVATE --coverage)
endif()

# aurora_core (webgpu/gpu.cpp) and aurora_gx reference each other; group them so the
# linker rescans and resolves the cycle. Same construct, same reason, as the Prime 1 port.
if(MSVC)
    target_link_libraries(metroid_prime2_port PRIVATE mp_game aurora::main aurora::core aurora::gx)
    target_link_options(metroid_prime2_port PRIVATE /INCREMENTAL:NO)
elseif(CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED OR CMAKE_CXX_LINK_GROUP_USING_RESCAN_SUPPORTED)
    target_link_libraries(metroid_prime2_port PRIVATE
        mp_game
        aurora::main
        "$<LINK_GROUP:RESCAN,aurora::core,aurora::gx>")
    if(ANDROID)
        # `log`: platform code logs through __android_log_print under __ANDROID__.
        # `android`: SDL's own Java-side glue and the native activity's JNI calls.
        # `z`: the vendored zlib-ng/zlib Aurora links is built by Aurora itself, but the
        # port's own disc/font inflation resolves zlib symbols against the platform one.
        target_link_libraries(metroid_prime2_port PRIVATE log android z)
        # SDL's Java activity looks the entry point up by name (dlsym "SDL_main"), which
        # the linker cannot see from inside the library, so keep it against -O2's GC.
        target_link_options(metroid_prime2_port PRIVATE "-Wl,-u,SDL_main")
        # Custom Vulkan drivers (Turnip) through libadrenotools (port_gpu_driver.cpp). Its
        # hook libraries and the shim Dawn loads as libvulkan.so are separate shared
        # libraries, loaded by path: android/app/build.gradle lists them as targets.
        if(ANDROID_ABI STREQUAL "arm64-v8a")
            add_subdirectory(extern/adrenotools EXCLUDE_FROM_ALL)
            add_library(mp2_vkshim SHARED platform/android/vkshim.c)
            set_target_properties(mp2_vkshim PROPERTIES C_VISIBILITY_PRESET hidden)
            target_link_libraries(metroid_prime2_port PRIVATE adrenotools)
            # port_gpu_driver.cpp is compiled in mp_port_audio: it needs the headers.
            target_link_libraries(mp_port_audio PRIVATE adrenotools)
        endif()
    else()
        # `platform/main.cpp` installs a SIGSEGV/SIGBUS/SIGILL/SIGFPE handler that calls
        # `backtrace_symbols_fd`, and **without -rdynamic the frames come back as bare
        # addresses** - which is the one form of this output that is not worth having, because
        # the whole point is naming the function that faulted. This is the only place it is
        # needed: the decomp build links no platform code and the gate's port build inherits
        # it from here. Android has no `execinfo`/`backtrace`, so it is desktop-only.
        target_link_options(metroid_prime2_port PRIVATE -rdynamic)
    endif()
elseif(APPLE)
    # ld64 searches every archive again until nothing new resolves, so the cycle needs no
    # group (as in the Prime 1 port). -rdynamic names the crash handler's frames, as above.
    target_link_libraries(metroid_prime2_port PRIVATE
        mp_game
        aurora::main aurora::core aurora::gx)
    target_link_options(metroid_prime2_port PRIVATE -rdynamic)
else()
    message(FATAL_ERROR
        "This port needs an ELF linker with RESCAN support, Apple ld or an MSVC-compatible COFF linker")
endif()

# The per-device button glyphs and the binding icons (tools/make_prompt_glyphs.py). Desktop builds
# (MP_EMBED_RESOURCES) carry them inside the executable, so a packaged folder needs no loose copies and
# a new executable dropped into an old install ignores the stale files left there. Without embedding
# they are loose files the port reads from a `textures` folder beside the executable
# (platform/main.cpp); on Android the APK packs them as assets and the activity unpacks them.
if(MP_EMBED_RESOURCES)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    file(GLOB_RECURSE MP_EMBED_INPUTS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/textures/*")
    # The file list is an input too, so a deleted texture regenerates the source.
    string(JOIN "\n" MP_EMBED_LIST ${MP_EMBED_INPUTS})
    set(MP_EMBED_LIST_FILE "${CMAKE_CURRENT_BINARY_DIR}/generated/port_embedded_inputs.txt")
    file(CONFIGURE OUTPUT "${MP_EMBED_LIST_FILE}" CONTENT "${MP_EMBED_LIST}\n" @ONLY)
    set(MP_EMBED_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/port_embedded_data.cpp")
    add_custom_command(
        OUTPUT "${MP_EMBED_SOURCE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
        COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_resources.py"
            "${MP_EMBED_SOURCE}"
            --dir "${CMAKE_CURRENT_SOURCE_DIR}/textures" textures
        DEPENDS ${MP_EMBED_INPUTS} "${MP_EMBED_LIST_FILE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_resources.py"
        COMMENT "Embedding textures"
        VERBATIM)
    target_sources(metroid_prime2_port PRIVATE "${MP_EMBED_SOURCE}")
    target_compile_definitions(metroid_prime2_port PRIVATE MP_EMBED_RESOURCES)
    if(TARGET mp_port_audio)
        target_compile_definitions(mp_port_audio PRIVATE MP_EMBED_RESOURCES)
    endif()
elseif(NOT ANDROID)
    # The generated sets are cleared first: copy_directory only adds, and an icon the generator no
    # longer writes would otherwise keep replacing a texture it no longer claims.
    set(PORT_TEXTURE_SETS bindings keyboard playstation switch xbox)
    list(TRANSFORM PORT_TEXTURE_SETS PREPEND "$<TARGET_FILE_DIR:metroid_prime2_port>/textures/")
    add_custom_command(TARGET metroid_prime2_port POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${PORT_TEXTURE_SETS}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/textures"
            "$<TARGET_FILE_DIR:metroid_prime2_port>/textures"
        COMMENT "Copying texture replacements next to the executable")
endif()

# Install rules (desktop only): the executable, and for Linux menus and software centres the desktop
# entry (Categories=Game), its icon under the app id, and the AppStream metainfo (category Game).
if(NOT ANDROID AND NOT IOS)
    include(GNUInstallDirs)
    install(TARGETS metroid_prime2_port RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(NOT MP_EMBED_RESOURCES)
        install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/textures DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
    if(UNIX AND NOT APPLE)
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/io.github.odrannnn.metroidprime2port.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/varia-bolt.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
            RENAME io.github.odrannnn.metroidprime2port.svg)
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/io.github.odrannnn.metroidprime2port.metainfo.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    endif()
endif()
