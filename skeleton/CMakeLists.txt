cmake_minimum_required(VERSION 3.25)
project(krNN LANGUAGES CXX)

# A VM tree that builds a machine, its guest runtime and its tests. Replace every
# `krNN` and `KRNN` below; nothing else here should need editing to bring up a new
# architecture, which is what docs/new-target-guide.md measures.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /WX /permissive- /Zc:__cplusplus /Brepro)
    add_link_options(/Brepro)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()

# The compiler is resolved by the one shared module, so this tree answers "where is
# krc" the same way every other VM does. Copying it per tree is deliberate: four
# identical copies beat four slightly different answers.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/KrGuest.cmake")
krguest_find_toolchain()

# Host code lives in src/, guest code in srcNN/ and is compiled by
# `krc --target=krNN`, never by the host compiler. The boundary is which compiler
# builds the file; CheckBoundary enforces it so a guest source cannot be pulled into
# the host build by accident.
add_library(krNN_core STATIC
    src/machine.cpp
)
target_include_directories(krNN_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# The `::core` export is what krdbg and any other consumer links; it is exported
# from this tree rather than reimplemented there.
install(TARGETS krNN_core EXPORT krNNTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(EXPORT krNNTargets
    FILE krNN-targets.cmake
    NAMESPACE krNN::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/krNN")

add_executable(krNN src/main.cpp)
target_link_libraries(krNN PRIVATE krNN_core)
install(TARGETS krNN RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

# The guest tests need krc and krld. With the option ON their absence stops the
# configure; with it OFF no row is registered and the configure says so, because a
# suite that registers nothing reports success.
option(KRNN_BUILD_GUEST_TESTS "Build the guest program tests, which need krc and krld" OFF)
if(KRNN_BUILD_GUEST_TESTS)
    add_subdirectory(tests)
endif()

# The gate a family run invokes: build, then run every row, with the job count the
# family hands down.
add_custom_target(gate
    COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure
    DEPENDS krNN)
