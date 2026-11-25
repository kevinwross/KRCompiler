cmake_minimum_required(VERSION 3.25)

project(KRCompiler VERSION 0.2.0 LANGUAGES CXX)

# Build provenance for krc --version: an image can say which compiler built it
# only if the compiler can say what it is. The revision is optional -- a source
# tree without git still builds, and says "unknown" rather than guessing.
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE KRC_BUILD_REVISION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT KRC_BUILD_REVISION)
    set(KRC_BUILD_REVISION "unknown")
endif()
if(CMAKE_BUILD_TYPE)
    set(KRC_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
elseif(CMAKE_CONFIGURATION_TYPES)
    # A multi-config generator has no single configuration to name; the binary
    # reports the one it was built for, and the stamp says so.
    string(REPLACE ";" "/" KRC_CONFIG_LIST "${CMAKE_CONFIGURATION_TYPES}")
    set(KRC_BUILD_TYPE "multi-config ${KRC_CONFIG_LIST}")
else()
    set(KRC_BUILD_TYPE "unspecified")
endif()
set(KRC_BUILD_COMPILER "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
string(CONFIGURE
    "#pragma once\n\n#define KRC_BUILD_REVISION \"@KRC_BUILD_REVISION@\"\n#define KRC_BUILD_COMPILER \"@KRC_BUILD_COMPILER@\"\n"
    KRC_BUILD_STAMP_CONTENT @ONLY)
file(WRITE "${CMAKE_BINARY_DIR}/generated/krc/version_stamp.h" "${KRC_BUILD_STAMP_CONTENT}")
# Every tool that reports its version reads the stamp, so the directory is on the
# include path of the whole build rather than of one target at a time.
include_directories("${CMAKE_BINARY_DIR}/generated")
# The configuration is a per-configuration fact, so it travels as a generator
# expression rather than being frozen into the generated header: a multi-config
# generator has no CMAKE_BUILD_TYPE at configure time to freeze.
add_compile_definitions($<$<COMPILE_LANGUAGE:CXX>:KRC_BUILD_TYPE="$<CONFIG>">)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/host/bin")
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/host/bin/${config}")
endforeach()

set(KRC_STAGE_DIR "${CMAKE_BINARY_DIR}/stage")
set(KRC_STAGE_SYSROOT "${KRC_STAGE_DIR}/sysroot")
set(KRC_STAGE_RUNTIME "${KRC_STAGE_SYSROOT}/kr32/lib")
set(KRC_STAGE_RUNTIME_KR16 "${KRC_STAGE_SYSROOT}/kr16/lib")
set(KRC_STAGE_RUNTIME_KR232 "${KRC_STAGE_SYSROOT}/kr232/lib")
set(KRC_TEST_OUTPUT_DIR "${KRC_STAGE_DIR}/tests")
file(MAKE_DIRECTORY "${KRC_TEST_OUTPUT_DIR}")

include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Determinism.cmake)

option(KRC_BUILD_TESTS "Build the KRCompiler test suite" ON)
option(KRC_BUILD_TOOLS "Build krc, krld, and the fixture extractor" ON)
option(KRC_BUILD_TARGET_RUNTIME "Build target runtime objects with krc" ON)

# Executed tests need a KR32 VM. A copy kept in this tree is preferred over the
# sibling checkout, because sibling agents rebuild and run their own binaries in
# those directories, so a test that launches one there can be killed mid-run. The
# copy lives in `vm/`, not under `build/`: `build/` is deleted for a clean gate and
# the binaries are not reproducible from this tree, so a gate that removed them
# would silently drop the executed tests while the suite stayed green. `build/vm/`
# is read after `vm/` so a checkout that still keeps the copies there keeps
# working. The lookup uses the host's executable suffix, so a Windows host picks up
# `krc_kr32.exe` and a Linux host `krc_kr32`; both then run the executed tests
# instead of leaving them unregistered. The sibling fallbacks are Windows builds
# and are tried only there. An empty result still registers no run tests, which
# keeps a host without a VM green.
set(KRC_KR32_VM "" CACHE FILEPATH "Path to a kr32 VM binary for the executed tests")
if(NOT KRC_KR32_VM)
    foreach(_krc_vm_dir IN ITEMS
            "${CMAKE_CURRENT_SOURCE_DIR}/vm"
            "${CMAKE_CURRENT_SOURCE_DIR}/build/vm")
        if(NOT KRC_KR32_VM AND EXISTS "${_krc_vm_dir}/krc_kr32${CMAKE_EXECUTABLE_SUFFIX}")
            set(KRC_KR32_VM "${_krc_vm_dir}/krc_kr32${CMAKE_EXECUTABLE_SUFFIX}"
                CACHE FILEPATH "Path to a kr32 VM binary for the executed tests" FORCE)
        endif()
    endforeach()
    if(WIN32 AND NOT KRC_KR32_VM)
        set(_krc_sibling_vm "${CMAKE_CURRENT_SOURCE_DIR}/../kr32/build/windows-msvc/Debug/kr32.exe")
        if(EXISTS "${_krc_sibling_vm}")
            set(KRC_KR32_VM "${_krc_sibling_vm}" CACHE FILEPATH
                "Path to a kr32 VM binary for the executed tests" FORCE)
        endif()
    endif()
endif()

# The KR16 VM comes from the same place by the same rule. Its `vm` command takes
# an image and reports the program's exit status, so one runner script covers both
# targets.
set(KRC_KR16_VM "" CACHE FILEPATH "Path to a kr16 VM binary for the executed tests")
if(NOT KRC_KR16_VM)
    foreach(_krc_vm_dir IN ITEMS
            "${CMAKE_CURRENT_SOURCE_DIR}/vm"
            "${CMAKE_CURRENT_SOURCE_DIR}/build/vm")
        if(NOT KRC_KR16_VM AND EXISTS "${_krc_vm_dir}/krc_kr16${CMAKE_EXECUTABLE_SUFFIX}")
            set(KRC_KR16_VM "${_krc_vm_dir}/krc_kr16${CMAKE_EXECUTABLE_SUFFIX}"
                CACHE FILEPATH "Path to a kr16 VM binary for the executed tests" FORCE)
        endif()
    endforeach()
    if(WIN32 AND NOT KRC_KR16_VM)
        foreach(_krc_sibling_vm IN ITEMS
                "${CMAKE_CURRENT_SOURCE_DIR}/../kr16/build/windows-ninja/kr16.exe"
                "${CMAKE_CURRENT_SOURCE_DIR}/../kr16/build/shell-debug/kr16.exe")
            if(NOT KRC_KR16_VM AND EXISTS "${_krc_sibling_vm}")
                set(KRC_KR16_VM "${_krc_sibling_vm}" CACHE FILEPATH
                    "Path to a kr16 VM binary for the executed tests" FORCE)
            endif()
        endforeach()
    endif()
endif()

# The kr232 emulator is the sibling repository's `kr232`, by the same rule as
# the other two: a copy kept in this tree is preferred, the sibling checkout is a
# Windows-only fallback, and an empty result registers no run test so a host
# without the emulator stays green.
set(KRC_KR232_VM "" CACHE FILEPATH "Path to a kr232 VM binary for the executed tests")
set(KRC_KR232_BLANK_IMAGE
    "${CMAKE_CURRENT_SOURCE_DIR}/../kr232/tests/fixtures/blank-flip.kr232"
    CACHE FILEPATH "A program that commits the surface without drawing: no drawing golden may equal its frame")
if(NOT KRC_KR232_VM)
    foreach(_krc_vm_dir IN ITEMS
            "${CMAKE_CURRENT_SOURCE_DIR}/vm"
            "${CMAKE_CURRENT_SOURCE_DIR}/build/vm")
        if(NOT KRC_KR232_VM AND EXISTS "${_krc_vm_dir}/kr232${CMAKE_EXECUTABLE_SUFFIX}")
            set(KRC_KR232_VM "${_krc_vm_dir}/kr232${CMAKE_EXECUTABLE_SUFFIX}"
                CACHE FILEPATH "Path to a kr232 VM binary for the executed tests" FORCE)
        endif()
    endforeach()
    if(WIN32 AND NOT KRC_KR232_VM)
        set(_krc_sibling_vm
            "${CMAKE_CURRENT_SOURCE_DIR}/../kr232/build/windows-msvc/kr232.exe")
        if(EXISTS "${_krc_sibling_vm}")
            set(KRC_KR232_VM "${_krc_sibling_vm}" CACHE FILEPATH
                "Path to a kr232 VM binary for the executed tests" FORCE)
        endif()
    endif()
endif()

# The implementation is an object library, not an installable target. The public
# surface is `krc::toolchain`, whose archive carries these objects; `krc_impl`
# never appears in the exported package.
add_library(krc_impl OBJECT
    src/asm/kro_writer.cpp
    src/asm/kr32_assembler.cpp
    src/asm/kr32_encoder.cpp
    src/asm/kr16_encoder.cpp
    src/asm/kr216_encoder.cpp
    src/asm/kr16_assembler.cpp
    src/asm/kr16_object.cpp
    src/asm/kr216_object.cpp
    src/backend/kr32_backend.cpp
    src/backend/kr16_backend.cpp
    src/backend/kr216_backend.cpp
    src/backend/machine_ir.cpp
    src/backend/machine_liveness.cpp
    src/backend/machine_cfg.cpp
    src/backend/kr232_backend.cpp
    src/backend/kr232_cfg.cpp
    src/backend/kr232_descriptor.cpp
    src/asm/kr232_assembler.cpp
    src/asm/kr232_encoder.cpp
    src/asm/kr232_object.cpp
    src/image/k232.cpp
    src/linker/kr232_image.cpp
    src/asm/kr232_parser.cpp
    src/backend/kr32_descriptor.cpp
    src/backend/peephole.cpp
    src/backend/register_allocator.cpp
    src/frontend/ast.cpp
    src/linker/kro_object.cpp
    src/linker/kr16_image.cpp
    src/linker/kr216_image.cpp
    src/linker/archive.cpp
    src/linker/linker.cpp
    src/frontend/diagnostics.cpp
    src/frontend/lexer.cpp
    src/frontend/parser.cpp
    src/frontend/preprocessor.cpp
    src/frontend/sema.cpp
    src/frontend/source_manager.cpp
    src/frontend/types.cpp
    src/ir/ir.cpp
    src/ir/mem2reg.cpp
    src/ir/passes.cpp
    src/irgen/c_irgen.cpp
    src/irgen/c_irgen_expressions.cpp
    src/irgen/c_irgen_setup.cpp
    src/target/sysroot_bits.cpp
    src/target/target.cpp
)
target_include_directories(krc_impl PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_BINARY_DIR}/generated")
target_include_directories(krc_impl PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(krc_impl PUBLIC cxx_std_20)
krc_apply_warnings(krc_impl)
krc_apply_sanitizers(krc_impl)

# `krc::debug` is the KDBG reader/writer and depends only on the standard
# library, so a host that only decodes images links it alone.
add_library(krc_debug STATIC
    src/debug/kdbg.cpp
)
add_library(krc::debug ALIAS krc_debug)
set_target_properties(krc_debug PROPERTIES
    OUTPUT_NAME krc-debug
    EXPORT_NAME debug
)
target_include_directories(krc_debug PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(krc_debug PUBLIC cxx_std_20)
krc_apply_warnings(krc_debug)
krc_apply_sanitizers(krc_debug)

# `krc::toolchain` is the public build facade. Its archive carries the compiler
# implementation, so a host links one static library and gets compile, assemble
# and link without a subprocess or a second entry point.
add_library(krc_toolchain STATIC
    src/toolchain/toolchain.cpp
    $<TARGET_OBJECTS:krc_impl>
)
add_library(krc::toolchain ALIAS krc_toolchain)
set_target_properties(krc_toolchain PROPERTIES
    OUTPUT_NAME krc-toolchain
    EXPORT_NAME toolchain
)
target_include_directories(krc_toolchain PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(krc_toolchain PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(krc_toolchain PRIVATE krc_debug)
target_compile_features(krc_toolchain PUBLIC cxx_std_20)
krc_apply_warnings(krc_toolchain)
krc_apply_sanitizers(krc_toolchain)

add_library(krc_fixture_extractor STATIC
    src/tools/fixture_extractor.cpp
)
target_include_directories(krc_fixture_extractor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(krc_fixture_extractor PUBLIC krc_toolchain)
target_compile_features(krc_fixture_extractor PUBLIC cxx_std_20)
krc_apply_warnings(krc_fixture_extractor)
krc_apply_sanitizers(krc_fixture_extractor)

if(KRC_BUILD_TOOLS)
    add_executable(krc_gen_sysroot_bits src/tools/gen_sysroot_bits/main.cpp)
    target_link_libraries(krc_gen_sysroot_bits PRIVATE krc_toolchain)
    krc_apply_warnings(krc_gen_sysroot_bits)
    krc_apply_sanitizers(krc_gen_sysroot_bits)

    # `ALL`: a stage that only refreshes when this target is named by hand goes
    # stale the first time someone adds a header, and the install then ships a
    # sysroot missing it -- which is how stdarg.h was absent from both prefixes.
    add_custom_target(krc_stage_sysroot ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${KRC_STAGE_SYSROOT}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/sysroot/include"
                "${KRC_STAGE_SYSROOT}/include"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/sysroot/kr32/include"
                "${KRC_STAGE_SYSROOT}/kr32/include"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/sysroot/kr16/include"
                "${KRC_STAGE_SYSROOT}/kr16/include"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/sysroot/kr64/include"
                "${KRC_STAGE_SYSROOT}/kr64/include"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/sysroot/kr232/include"
                "${KRC_STAGE_SYSROOT}/kr232/include"
        COMMAND $<TARGET_FILE:krc_gen_sysroot_bits> --out "${KRC_STAGE_SYSROOT}"
        COMMENT "Staging target sysroot headers and generated bits"
        VERBATIM)
    add_dependencies(krc_stage_sysroot krc_gen_sysroot_bits)

    add_executable(krc src/tools/krc/main.cpp)
# The build stamp lives in the build tree, so the tool needs that include path.
target_include_directories(krc PRIVATE "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(krc PRIVATE krc::toolchain)
    target_compile_definitions(krc PRIVATE
        KRC_SYSROOT_DIR="${KRC_STAGE_SYSROOT}")
    krc_apply_warnings(krc)
    krc_apply_sanitizers(krc)
    add_dependencies(krc krc_stage_sysroot)

    add_executable(krld src/tools/krld/main.cpp)
    target_link_libraries(krld PRIVATE krc::toolchain)
    target_include_directories(krld PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    krc_apply_warnings(krld)
    krc_apply_sanitizers(krld)

    add_executable(krc_extract_fixtures src/tools/extract_fixtures/main.cpp)
    target_link_libraries(krc_extract_fixtures PRIVATE krc_fixture_extractor)
    krc_apply_warnings(krc_extract_fixtures)
    krc_apply_sanitizers(krc_extract_fixtures)

    if(KRC_BUILD_TARGET_RUNTIME)
        # Each staged object is written under a temporary name and renamed into
        # place. A reader that opens the final path must never see a partially
        # written object: a truncated read makes `krld` report a KRO CRC
        # mismatch on a runtime that is in fact correct.
        add_custom_target(krc_target_runtime_kr32 ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory "${KRC_STAGE_RUNTIME}"
            COMMAND $<TARGET_FILE:krc> --target=kr32 -c
                    "${CMAKE_CURRENT_SOURCE_DIR}/targets/kr32/runtime/libc.c"
                    -o "${KRC_STAGE_RUNTIME}/libkrcrt.kro.new"
            COMMAND ${CMAKE_COMMAND} -E rename
                    "${KRC_STAGE_RUNTIME}/libkrcrt.kro.new"
                    "${KRC_STAGE_RUNTIME}/libkrcrt.kro"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/targets/kr32/runtime/crt0.s"
                    "${KRC_STAGE_RUNTIME}/crt0.s"
            COMMENT "Building the staged KR32 runtime"
            VERBATIM)
        add_dependencies(krc_target_runtime_kr32 krc krc_stage_sysroot)

        add_custom_target(krc_target_runtime_kr16 ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory "${KRC_STAGE_RUNTIME_KR16}"
            COMMAND $<TARGET_FILE:krc> --target=kr16 -c --emit-object
                    "${CMAKE_CURRENT_SOURCE_DIR}/targets/kr16/runtime/libc.c"
                    -o "${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro.new"
            COMMAND ${CMAKE_COMMAND} -E rename
                    "${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro.new"
                    "${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/targets/kr16/runtime/crt0.s"
                    "${KRC_STAGE_RUNTIME_KR16}/crt0.s"
            COMMAND $<TARGET_FILE:krc> --target=kr16 -c --emit-object
                    "${CMAKE_CURRENT_SOURCE_DIR}/targets/kr16/runtime/host.s"
                    -o "${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro.new"
            COMMAND ${CMAKE_COMMAND} -E rename
                    "${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro.new"
                    "${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro"
            COMMAND $<TARGET_FILE:krc> --target=kr16 -c --emit-object
                    "${CMAKE_CURRENT_SOURCE_DIR}/targets/kr16/runtime/fileio.c"
                    -o "${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro.new"
            COMMAND ${CMAKE_COMMAND} -E rename
                    "${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro.new"
                    "${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro"
            COMMENT "Building the staged KR16 runtime"
            VERBATIM)
        add_dependencies(krc_target_runtime_kr16 krc krc_stage_sysroot)

        # The kr232 packages. A program that includes <kr232/cons.h>, gfx.h, fb.h
        # or input.h links these objects; without them the header declares a
        # function no object defines and every such program fails at link time
        # with an undefined symbol. Each object is written to a temporary name and
        # renamed, for the same reason as the runtimes above.
        set(KRC_KR232_PACKAGE_SOURCES
            cons_user.c gfx_user.c fb_user.c input_user.c syscall6.s)
        set(_kr232_package_commands
            COMMAND ${CMAKE_COMMAND} -E make_directory "${KRC_STAGE_RUNTIME_KR232}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/targets/kr232/runtime/crt0.s"
                    "${KRC_STAGE_RUNTIME_KR232}/crt0.s")
        foreach(package IN LISTS KRC_KR232_PACKAGE_SOURCES)
            get_filename_component(_stem "${package}" NAME_WE)
            list(APPEND _kr232_package_commands
                COMMAND $<TARGET_FILE:krc> --target=kr232 -c
                        "--sysroot=${KRC_STAGE_SYSROOT}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/targets/kr232/runtime/${package}"
                        -o "${KRC_STAGE_RUNTIME_KR232}/${_stem}.kro.new"
                COMMAND ${CMAKE_COMMAND} -E rename
                        "${KRC_STAGE_RUNTIME_KR232}/${_stem}.kro.new"
                        "${KRC_STAGE_RUNTIME_KR232}/${_stem}.kro")
        endforeach()
        add_custom_target(krc_target_runtime_kr232 ALL
            ${_kr232_package_commands}
            COMMENT "Building the staged kr232 packages"
            VERBATIM)
        add_dependencies(krc_target_runtime_kr232 krc krc_stage_sysroot)
    endif()
endif()

if(KRC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# The exported package. Consumers see exactly two targets — `krc::toolchain` and
# `krc::debug` — and the headers under include/krc. The compiler implementation
# is compiled into krc-toolchain's archive and never exported as a target.
install(TARGETS krc_toolchain krc_debug
    EXPORT krcTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(DIRECTORY include/krc DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(EXPORT krcTargets
    NAMESPACE krc::
    FILE krc-targets.cmake
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/krc"
)
# A stamp describing this prefix, installed beside the package config so a
# consumer can read it without running anything. It is written at configure time
# and therefore cannot name the configuration of a multi-config build; the
# binary itself does, through `krc --version`, which is the answer that matters
# when two prefixes differ only in how they were built.
file(WRITE "${CMAKE_BINARY_DIR}/krc-stamp.cmake"
    "# Generated by the KRCompiler build; describes this prefix.\n"
    "set(KRC_STAMP_VERSION \"${PROJECT_VERSION}\")\n"
    "set(KRC_STAMP_BUILD \"${KRC_BUILD_TYPE}\")\n"
    "set(KRC_STAMP_REVISION \"${KRC_BUILD_REVISION}\")\n"
    "set(KRC_STAMP_COMPILER \"${KRC_BUILD_COMPILER}\")\n")
install(FILES "${CMAKE_BINARY_DIR}/krc-stamp.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/krc")

# The target sysroot and runtime. A consumer builds a runnable image from the
# install alone: the staged sysroot carries the headers, the generated bits and
# `libkrcrt.kro`, and the KR32 runtime assembly is installed beside them.
# `KRC_SYSROOT` and `KRC_KR32_RUNTIME_DIR` are exported by the package config.
set(KRC_INSTALL_SYSROOT_REL "${CMAKE_INSTALL_DATADIR}/krc/sysroot")
if(KRC_BUILD_TOOLS)
    # The build chain itself: a host that gets the package can run the compiler
    # and linker from <prefix>/bin instead of a KRCompiler build tree. This is
    # the same prefix rule the libraries use; the executables are not exported
    # as CMake targets.
    install(TARGETS krc krld
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(DIRECTORY "${KRC_STAGE_SYSROOT}/"
        DESTINATION "${KRC_INSTALL_SYSROOT_REL}")
    install(FILES
        targets/kr32/runtime/crt0.s
        targets/kr32/runtime/fileio.s
        targets/kr32/runtime/eh.s
        DESTINATION "${KRC_INSTALL_SYSROOT_REL}/kr32/lib")
else()
    message(STATUS
        "KRC_BUILD_TOOLS=OFF: the installed package carries no target sysroot or runtime")
endif()

foreach(package IN ITEMS krc-toolchain krc-debug)
    configure_package_config_file(
        "cmake/${package}-config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${package}-config.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${package}"
    )
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/${package}-config-version.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY SameMajorVersion
    )
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/${package}-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${package}-config-version.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${package}"
    )
endforeach()
