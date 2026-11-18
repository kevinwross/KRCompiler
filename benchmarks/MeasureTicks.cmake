# Performance baseline for the optimiser work.
#
# Compiles and links benchmarks/programs, runs each under the VM with --stats, and records
# the `ticks` counter. Ticks are an instruction count, so the number is deterministic: a
# pass that removes work moves it, and nothing else does. Wall-clock would be noise.
#
# Invoked by cmake -P:
#   cmake -DKRC=<krc> -DKRLD=<krld> -DROOT=<repo> -DKR32VM=<vm> -DKR16VM=<vm> \
#         -DOUT=<file.tsv> -P MeasureTicks.cmake
#
# Each program returns 42 only when its kernels compute the values recorded in the source,
# so a wrong result is a failed measurement rather than a fast one.

if(NOT KRC OR NOT KRLD OR NOT ROOT OR NOT OUT)
    message(FATAL_ERROR "KRC, KRLD, ROOT and OUT are required")
endif()

set(PROGRAM_DIR "${ROOT}/benchmarks/programs")
set(WORK_DIR "${ROOT}/build/benchmarks")
file(MAKE_DIRECTORY "${WORK_DIR}")

file(GLOB PROGRAMS "${PROGRAM_DIR}/*.c")
list(SORT PROGRAMS)
if(NOT PROGRAMS)
    message(FATAL_ERROR "no programs in ${PROGRAM_DIR}")
endif()

file(WRITE "${OUT}" "program\ttarget\tticks\texit\tstatus\n")

foreach(program IN LISTS PROGRAMS)
    get_filename_component(name "${program}" NAME_WE)

    # KR32: the image links against the staged runtime through the same script the tests use.
    set(image32 "${WORK_DIR}/${name}.k32")
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
                "-DKRC=${KRC}" "-DKRLD=${KRLD}" "-DSOURCE=${program}" "-DOUTPUT=${image32}"
                -P "${ROOT}/cmake/LinkRuntime.cmake"
        RESULT_VARIABLE link32 OUTPUT_QUIET ERROR_QUIET)

    if(KR32VM AND EXISTS "${KR32VM}")
        if(link32 EQUAL 0 AND EXISTS "${image32}")
            execute_process(
                COMMAND "${KR32VM}" vm "${image32}" --stats
                RESULT_VARIABLE run32 OUTPUT_VARIABLE out32 ERROR_QUIET)
            string(REGEX MATCH "\nticks=([0-9]+)" _ "${out32}")
            file(APPEND "${OUT}" "${name}\tkr32\t${CMAKE_MATCH_1}\t${run32}\tok\n")
        else()
            file(APPEND "${OUT}" "${name}\tkr32\t-\t-\tlink-failed\n")
        endif()
    else()
        file(APPEND "${OUT}" "${name}\tkr32\t-\t-\tno-vm\n")
    endif()

    # KR16: host-linked .kex through its own script, as the tests do.
    set(image16 "${WORK_DIR}/${name}.kex")
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
                "-DKRC=${KRC}" "-DKRLD=${KRLD}" "-DSOURCE=${program}" "-DOUTPUT=${image16}"
                -P "${ROOT}/cmake/LinkKr16Host.cmake"
        RESULT_VARIABLE link16 OUTPUT_QUIET ERROR_QUIET)

    if(KR16VM AND EXISTS "${KR16VM}")
        if(link16 EQUAL 0 AND EXISTS "${image16}")
            execute_process(
                COMMAND "${KR16VM}" vm "${image16}" --stats
                RESULT_VARIABLE run16 OUTPUT_VARIABLE out16 ERROR_QUIET)
            string(REGEX MATCH "\nticks=([0-9]+)" _ "${out16}")
            file(APPEND "${OUT}" "${name}\tkr16\t${CMAKE_MATCH_1}\t${run16}\tok\n")
        else()
            file(APPEND "${OUT}" "${name}\tkr16\t-\t-\tlink-failed\n")
        endif()
    else()
        file(APPEND "${OUT}" "${name}\tkr16\t-\t-\tno-vm\n")
    endif()
endforeach()

message(STATUS "tick baseline written to ${OUT}")
