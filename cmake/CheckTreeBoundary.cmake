# Enforce the host/target source boundary described by docs/tree.md.
if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "CheckTreeBoundary.cmake requires SOURCE")
endif()

file(GLOB_RECURSE host_target_sources
     "${SOURCE}/src/*.c"
     "${SOURCE}/src/*.s")
if(host_target_sources)
    string(REPLACE ";" "\n  " listed "${host_target_sources}")
    message(FATAL_ERROR
        "Target source must not live under src/:\n  ${listed}\n"
        "Move it under targets/.")
endif()

if(EXISTS "${SOURCE}/src/runtime")
    message(FATAL_ERROR "src/runtime must not exist; target runtime source belongs under targets/")
endif()

if(NOT EXISTS "${SOURCE}/targets/kr32/runtime/libc.c")
    message(FATAL_ERROR "targets/kr32/runtime/libc.c is missing")
endif()
if(NOT EXISTS "${SOURCE}/sysroot/include")
    message(FATAL_ERROR "sysroot/include is missing")
endif()
