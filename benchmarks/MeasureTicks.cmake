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

if(NOT KRC OR NOT KRLD OR NOT ROOT OR NOT OUT OR NOT STAGE)
    message(FATAL_ERROR "KRC, KRLD, ROOT, STAGE and OUT are required")
endif()

# The staged runtime, laid out by the build. Passing these is what the test registrations do;
# omitting them made both link scripts fail with a message about the output file rather than
# about the missing inputs.
file(TO_NATIVE_PATH "${STAGE}/kr32/lib/libkrcrt.kro" KR32_LIBC)
file(TO_NATIVE_PATH "${STAGE}/kr32/lib/crt0.s" KR32_CRT0)
file(TO_NATIVE_PATH "${STAGE}/kr16/lib/crt0.s" KR16_CRT0)
file(TO_NATIVE_PATH "${STAGE}/kr16/lib/libkrcrt_host.kro" KR16_HOST)
file(TO_NATIVE_PATH "${STAGE}/kr16/lib/libkrcrt_fileio.kro" KR16_FILEIO)
file(TO_NATIVE_PATH "${STAGE}/kr16/lib/libkrcrt.kro" KR16_LIBCRT)

# A Windows path handed to a nested cmake through -D can arrive with its backslashes eaten as
# escapes, and then execute_process fails to start the compiler at all -- it reports a status
# string rather than a compiler diagnostic. Forward slashes survive the trip and krc accepts
# them, so the nested invocations get the slash form.
string(REPLACE "\\" "/" KRC_FOR_CMAKE "${KRC}")
string(REPLACE "\\" "/" KRLD_FOR_CMAKE "${KRLD}")
message(STATUS "KRC=[${KRC}] KRC_FOR_CMAKE=[${KRC_FOR_CMAKE}]")

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
    file(TO_NATIVE_PATH "${program}" source_native)

    # KR32: the image links against the staged runtime through the same script the tests use.
    set(image32 "${WORK_DIR}/${name}.k32")
    file(TO_NATIVE_PATH "${image32}" image32_native)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
                "-DKRC=${KRC_FOR_CMAKE}" "-DKRLD=${KRLD_FOR_CMAKE}" "-DSOURCE=${source_native}" "-DOUTPUT=${image32_native}"
                "-DLIBC=${KR32_LIBC}" "-DRUNTIME=${KR32_CRT0}"
                -P "${ROOT}/cmake/LinkRuntime.cmake"
        RESULT_VARIABLE link32 OUTPUT_QUIET ERROR_VARIABLE err32)

    if(KR32VM AND EXISTS "${KR32VM}")
        if(NOT link32 EQUAL 0)
            message(STATUS "kr32 link error: ${err32}")
        endif()
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
    file(TO_NATIVE_PATH "${image16}" image16_native)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
                "-DKRC=${KRC_FOR_CMAKE}" "-DKRLD=${KRLD_FOR_CMAKE}" "-DSOURCE=${source_native}" "-DOUTPUT=${image16_native}"
                "-DCRT0=${KR16_CRT0}" "-DHOST=${KR16_HOST}" "-DFILEIO=${KR16_FILEIO}" "-DLIBCRT=${KR16_LIBCRT}"
                -P "${ROOT}/cmake/LinkKr16Host.cmake"
        RESULT_VARIABLE link16 OUTPUT_QUIET ERROR_VARIABLE err16)

    if(KR16VM AND EXISTS "${KR16VM}")
        if(NOT link16 EQUAL 0)
            message(STATUS "kr16 link error: ${err16}")
        endif()
        if(link16 EQUAL 0 AND EXISTS "${image16}")
            execute_process(
                COMMAND "${KR16VM}" vm "${image16}" --stats
                RESULT_VARIABLE run16 OUTPUT_VARIABLE out16 ERROR_VARIABLE out16_err)
            # The two VMs agree on neither the label nor the stream: KR32 prints "ticks=N"
            # on stdout, KR16 prints "cycles=N instructions=M" on stderr. Searching both
            # keeps the runner honest about a machine difference instead of quietly
            # recording nothing.
            set(out16 "${out16}\n${out16_err}")
            string(REGEX MATCH "\nticks=([0-9]+)" _ "${out16}")
            # The KR16 VM labels the same quantity differently: it prints
            # "cycles=N instructions=M" where KR32 prints "ticks=N". Either is a
            # deterministic count; instructions is the closer match to ticks, so it is
            # used when the ticks line is absent.
            if("${CMAKE_MATCH_1}" STREQUAL "")
                string(REGEX MATCH "instructions=([0-9]+)" _ "${out16}")
            endif()
            file(APPEND "${OUT}" "${name}\tkr16\t${CMAKE_MATCH_1}\t${run16}\tok\n")
        else()
            file(APPEND "${OUT}" "${name}\tkr16\t-\t-\tlink-failed\n")
        endif()
    else()
        file(APPEND "${OUT}" "${name}\tkr16\t-\t-\tno-vm\n")
    endif()
endforeach()

message(STATUS "tick baseline written to ${OUT}")
