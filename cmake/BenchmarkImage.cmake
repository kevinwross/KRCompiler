# Runs one linked image under a KR VM and records the retired-instruction count.
#
# The VMs print either `after <n> steps` or `ticks=<n>` when they halt, which is
# the dynamic count this project has without adding a profiler to every target.
# The record is a TSV row so several builds can be collected into one file and
# compared.
#
# Required: VM, IMAGE, NAME, OUTPUT
# Optional: ARGS (a CMake list of guest arguments), MAX_STEPS (default 50000000)

if(NOT DEFINED VM OR NOT DEFINED IMAGE OR NOT DEFINED NAME OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "VM, IMAGE, NAME and OUTPUT are required")
endif()

if(NOT DEFINED MAX_STEPS OR MAX_STEPS STREQUAL "")
    set(MAX_STEPS 50000000)
endif()

set(guest_args)
if(DEFINED ARGS AND NOT ARGS STREQUAL "")
    list(APPEND guest_args ${ARGS})
endif()

execute_process(
    COMMAND "${VM}" vm "${IMAGE}" ${guest_args} --max-steps ${MAX_STEPS}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)

set(combined "${stdout}${stderr}")
if(combined MATCHES "after ([0-9]+) steps")
    set(steps "${CMAKE_MATCH_1}")
elseif(combined MATCHES "ticks=([0-9]+)")
    set(steps "${CMAKE_MATCH_1}")
else()
    message(FATAL_ERROR
        "benchmark ${NAME} printed no step count:\n${combined}")
endif()

file(APPEND "${OUTPUT}" "${NAME}\t${steps}\t${result}\n")
message(STATUS "${NAME}: ${steps} steps, exit ${result}")
