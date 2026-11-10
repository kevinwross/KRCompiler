cmake_minimum_required(VERSION 3.25)

project(KRCompiler VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin/${config}")
endforeach()

include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Determinism.cmake)

option(KRC_BUILD_TESTS "Build the KRCompiler test suite" ON)
option(KRC_BUILD_TOOLS "Build krc, krld, and the fixture extractor" ON)

add_library(krc_core STATIC
    src/asm/kro_writer.cpp
    src/asm/kr32_assembler.cpp
    src/asm/kr32_encoder.cpp
    src/asm/kr16_encoder.cpp
    src/asm/kr16_assembler.cpp
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
    set(KRC_GENERATED_SYSROOT_DIR "${CMAKE_BINARY_DIR}/generated-sysroot")
    add_executable(krc src/tools/krc/main.cpp)
    target_link_libraries(krc PRIVATE krc_core)
    target_compile_definitions(krc PRIVATE
        KRC_SYSROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/sysroot"
        KRC_GENERATED_SYSROOT_DIR="${KRC_GENERATED_SYSROOT_DIR}")
    krc_apply_warnings(krc)
    krc_apply_sanitizers(krc)

    add_executable(krc_gen_sysroot_bits src/tools/gen_sysroot_bits/main.cpp)
    target_link_libraries(krc_gen_sysroot_bits PRIVATE krc_core)
    krc_apply_warnings(krc_gen_sysroot_bits)
    krc_apply_sanitizers(krc_gen_sysroot_bits)

    add_custom_target(krc_generate_sysroot
        COMMAND $<TARGET_FILE:krc_gen_sysroot_bits> --out "${KRC_GENERATED_SYSROOT_DIR}"
        COMMENT "Generating target sysroot bits"
        VERBATIM)
    add_dependencies(krc_generate_sysroot krc_gen_sysroot_bits)
    add_dependencies(krc krc_generate_sysroot)

    add_executable(krld src/tools/krld/main.cpp)
    target_link_libraries(krld PRIVATE krc_core)
    krc_apply_warnings(krld)
    krc_apply_sanitizers(krld)

    add_executable(krc_extract_fixtures src/tools/extract_fixtures/main.cpp)
    target_link_libraries(krc_extract_fixtures PRIVATE krc_fixture_extractor)
    krc_apply_warnings(krc_extract_fixtures)
    krc_apply_sanitizers(krc_extract_fixtures)
endif()

if(KRC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
