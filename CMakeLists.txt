cmake_minimum_required(VERSION 3.25)

project(KRCompiler VERSION 0.1.0 LANGUAGES CXX)

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
set(KRC_TEST_OUTPUT_DIR "${KRC_STAGE_DIR}/tests")
file(MAKE_DIRECTORY "${KRC_TEST_OUTPUT_DIR}")

include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Determinism.cmake)

option(KRC_BUILD_TESTS "Build the KRCompiler test suite" ON)
option(KRC_BUILD_TOOLS "Build krc, krld, and the fixture extractor" ON)
option(KRC_BUILD_TARGET_RUNTIME "Build target runtime objects with krc" ON)

# Executed tests need a KR32 VM. The sibling checkout provides a Windows build,
# so the default is adopted only on a Windows host and only when that file
# exists: a WSL or Linux host must point KRC_KR32_VM at a binary it can run,
# and an empty value registers no run tests, which keeps such a host green.
set(KRC_KR32_VM "" CACHE FILEPATH "Path to a kr32 VM binary for the executed tests")
if(NOT KRC_KR32_VM AND WIN32)
    set(_krc_default_vm "${CMAKE_CURRENT_SOURCE_DIR}/../kr32/build/windows-msvc/Debug/kr32.exe")
    if(EXISTS "${_krc_default_vm}")
        set(KRC_KR32_VM "${_krc_default_vm}" CACHE FILEPATH
            "Path to a kr32 VM binary for the executed tests" FORCE)
    endif()
endif()

# The KR16 VM comes from the sibling checkout the same way. Its `vm` command
# takes an image and reports the program's exit status, so one runner script
# covers both targets.
set(KRC_KR16_VM "" CACHE FILEPATH "Path to a kr16 VM binary for the executed tests")
if(NOT KRC_KR16_VM AND WIN32)
    set(_krc_default_kr16_vm
        "${CMAKE_CURRENT_SOURCE_DIR}/../kr16/build/shell-debug/kr16.exe")
    if(EXISTS "${_krc_default_kr16_vm}")
        set(KRC_KR16_VM "${_krc_default_kr16_vm}" CACHE FILEPATH
            "Path to a kr16 VM binary for the executed tests" FORCE)
    endif()
endif()

add_library(krc_core STATIC
    src/asm/kro_writer.cpp
    src/asm/kr32_assembler.cpp
    src/asm/kr32_encoder.cpp
    src/asm/kr16_encoder.cpp
    src/asm/kr16_assembler.cpp
    src/asm/kr16_object.cpp
    src/backend/kr32_backend.cpp
    src/backend/kr16_backend.cpp
    src/backend/kr32_descriptor.cpp
    src/backend/peephole.cpp
    src/backend/register_allocator.cpp
    src/debug/kdbg.cpp
    src/frontend/ast.cpp
    src/linker/kro_object.cpp
    src/linker/kr16_image.cpp
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
    src/irgen/c_irgen.cpp
    src/target/sysroot_bits.cpp
    src/target/target.cpp
)
target_include_directories(krc_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_features(krc_core PUBLIC cxx_std_20)
krc_apply_warnings(krc_core)
krc_apply_sanitizers(krc_core)

add_library(krc_fixture_extractor STATIC
    src/tools/fixture_extractor.cpp
)
target_include_directories(krc_fixture_extractor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(krc_fixture_extractor PUBLIC krc_core)
target_compile_features(krc_fixture_extractor PUBLIC cxx_std_20)
krc_apply_warnings(krc_fixture_extractor)
krc_apply_sanitizers(krc_fixture_extractor)

if(KRC_BUILD_TOOLS)
    add_executable(krc_gen_sysroot_bits src/tools/gen_sysroot_bits/main.cpp)
    target_link_libraries(krc_gen_sysroot_bits PRIVATE krc_core)
    krc_apply_warnings(krc_gen_sysroot_bits)
    krc_apply_sanitizers(krc_gen_sysroot_bits)

    add_custom_target(krc_stage_sysroot
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
        COMMAND $<TARGET_FILE:krc_gen_sysroot_bits> --out "${KRC_STAGE_SYSROOT}"
        COMMENT "Staging target sysroot headers and generated bits"
        VERBATIM)
    add_dependencies(krc_stage_sysroot krc_gen_sysroot_bits)

    add_executable(krc src/tools/krc/main.cpp)
    target_link_libraries(krc PRIVATE krc_core)
    target_compile_definitions(krc PRIVATE
        KRC_SYSROOT_DIR="${KRC_STAGE_SYSROOT}")
    krc_apply_warnings(krc)
    krc_apply_sanitizers(krc)
    add_dependencies(krc krc_stage_sysroot)

    add_executable(krld src/tools/krld/main.cpp)
    target_link_libraries(krld PRIVATE krc_core)
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
    endif()
endif()

if(KRC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
