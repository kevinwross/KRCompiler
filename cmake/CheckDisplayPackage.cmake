# Installs the build tree into a private prefix and builds a separate CMake project against
# the installed package. This is the consumer evidence for krc-display: find_package must
# resolve, the imported target must carry the headers and the archive, and the linked program
# must run.
#
# Required: BUILD_DIR, WORK; optional CONFIG (the multi-config configuration to install/build).

foreach(required BUILD_DIR WORK)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/consumer")
set(prefix "${WORK}/prefix")

set(install_command "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${prefix}")
if(DEFINED CONFIG AND NOT CONFIG STREQUAL "")
    list(APPEND install_command --config "${CONFIG}")
endif()
execute_process(COMMAND ${install_command}
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "cmake --install failed (${result}):\n${output}${error}")
endif()

file(WRITE "${WORK}/consumer/CMakeLists.txt" [[
cmake_minimum_required(VERSION 3.25)
project(krc_display_consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(krc-display CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE krc::display)
]])

file(WRITE "${WORK}/consumer/main.cpp" [[
#include <cstdint>

#include <krc/display/display_service.h>
#include <krc/display/surface.h>
#include <krc/display/text_display.h>

namespace {

class Memory final : public krc::display::DisplayMemory {
public:
    std::uint32_t size() const override { return 0x10000; }
    bool valid_range(std::uint32_t address, std::uint32_t length) const override {
        return address <= size() && length <= size() - address;
    }
    bool read(std::uint32_t, std::uint8_t*, std::uint32_t) override { return true; }
    bool write(std::uint32_t, const std::uint8_t*, std::uint32_t) override { return true; }
};

}  // namespace

int main() {
    krc::display::TextDisplay display;
    if (display.rows() != 30 || display.cols() != 80) return 1;
    if (display.info().abi_version != krc::display::kAbiVersion) return 2;

    Memory memory;
    std::uint32_t block[krc::display::kCanonicalWords] = {};
    const std::int32_t status = krc::display::DisplayService::execute(
        display, memory, krc::display::Command::Info, block, 4);
    if (status != krc::display::kStatusOk) return 3;
    if (block[0] != krc::display::kAbiVersion) return 4;

    krc::display::Surface surface;
    if (!surface.set_mode(8, 8)) return 5;
    surface.clear(krc::display::kDefaultForeground);
    surface.flip();
    if (surface.frame_hash() == 0) return 6;
    return 0;
}
]])

set(configure_command "${CMAKE_COMMAND}" -S "${WORK}/consumer" -B "${WORK}/build"
    "-DCMAKE_PREFIX_PATH=${prefix}")
if(DEFINED CONFIG AND NOT CONFIG STREQUAL "")
    list(APPEND configure_command "-DCMAKE_BUILD_TYPE=${CONFIG}")
endif()
if(DEFINED CMAKE_CXX_COMPILER AND NOT CMAKE_CXX_COMPILER STREQUAL "")
    list(APPEND configure_command "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
endif()
if(DEFINED CMAKE_CXX_FLAGS AND NOT CMAKE_CXX_FLAGS STREQUAL "")
    list(APPEND configure_command "-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}")
endif()
if(DEFINED CMAKE_EXE_LINKER_FLAGS AND NOT CMAKE_EXE_LINKER_FLAGS STREQUAL "")
    list(APPEND configure_command "-DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}")
endif()
execute_process(COMMAND ${configure_command}
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "the consumer configure failed (${result}):\n${output}${error}")
endif()

set(build_command "${CMAKE_COMMAND}" --build "${WORK}/build")
if(DEFINED CONFIG AND NOT CONFIG STREQUAL "")
    list(APPEND build_command --config "${CONFIG}")
endif()
execute_process(COMMAND ${build_command}
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "the consumer build failed (${result}):\n${output}${error}")
endif()

if(WIN32)
    set(executable_suffix ".exe")
else()
    set(executable_suffix "")
endif()
set(candidates
    "${WORK}/build/${CONFIG}/consumer${executable_suffix}"
    "${WORK}/build/consumer${executable_suffix}")
set(executable "")
foreach(candidate IN LISTS candidates)
    if(EXISTS "${candidate}")
        set(executable "${candidate}")
        break()
    endif()
endforeach()
if(executable STREQUAL "")
    message(FATAL_ERROR "the consumer executable was not built; tried ${candidates}")
endif()

execute_process(COMMAND "${executable}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "the consumer failed with ${result}")
endif()

message(STATUS "the installed krc-display package configured, built and ran a consumer")
