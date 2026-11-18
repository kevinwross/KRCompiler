# Size baseline for the optimiser work.
#
# Compiles each workload for both targets and reports the emitted object size and the
# number of assembly instructions. Both are properties of the compiler's output, so a pass
# that makes code smaller or larger moves these numbers, and nothing else does.
#
# Invoked by benchmarks/measure.ps1 (or cmake -P directly):
#   cmake -DKRC=<krc> -DROOT=<repo> -DTARGETS=kr32;kr16 -DOUT=<file.tsv> -P MeasureSize.cmake
#
# Deliberately not a ctest: these numbers are expected to move as passes land, so asserting
# them would make every improvement a test failure.

if(NOT KRC)
    message(FATAL_ERROR "KRC is required")
endif()
if(NOT ROOT)
    message(FATAL_ERROR "ROOT is required")
endif()
if(NOT OUT)
    set(OUT "${ROOT}/benchmarks/size.tsv")
endif()
if(NOT TARGETS)
    set(TARGETS kr32 kr16)
endif()

# libc.c first, then the extracted corpus. libc.c is the largest real unit the compiler
# sees; the corpus adds the smaller shapes where a pass can behave differently.
set(WORKLOADS
    "${ROOT}/targets/kr32/runtime/libc.c"
    "${ROOT}/targets/kr16/runtime/libc.c")
file(GLOB CORPUS_SOURCES
    "${ROOT}/tests/corpus/extracted/tier1/*.c"
    "${ROOT}/tests/corpus/extracted/tier2/*.c"
    "${ROOT}/tests/corpus/extracted/tier2/*.cpp")
list(SORT CORPUS_SOURCES)
list(APPEND WORKLOADS ${CORPUS_SOURCES})

set(WORK_DIR "${ROOT}/build/benchmarks")
file(MAKE_DIRECTORY "${WORK_DIR}")

file(WRITE "${OUT}" "workload\ttarget\tobject_bytes\tinstructions\tstatus\n")

foreach(source IN LISTS WORKLOADS)
    get_filename_component(name "${source}" NAME)
    # The two runtime libraries are both called libc.c, so the row label and the work file
    # name carry the target directory. Without it the second object overwrites the first and
    # two rows claim to describe one workload.
    get_filename_component(dir "${source}" DIRECTORY)
    get_filename_component(dir "${dir}" DIRECTORY)
    get_filename_component(target_dir "${dir}" NAME)
    if(name STREQUAL "libc.c")
        set(label "${target_dir}/${name}")
    else()
        set(label "${name}")
    endif()
    string(REPLACE "/" "-" stem "${label}")
    foreach(target IN LISTS TARGETS)
        set(object "${WORK_DIR}/${stem}.${target}.kro")
        set(assembly "${WORK_DIR}/${stem}.${target}.s")

        execute_process(
            COMMAND "${KRC}" -c "--target=${target}" --emit-object "${source}"
                    -o "${object}"
            RESULT_VARIABLE compile_status
            OUTPUT_QUIET ERROR_QUIET)

        if(compile_status EQUAL 0 AND EXISTS "${object}")
            file(SIZE "${object}" object_bytes)

            execute_process(
                COMMAND "${KRC}" -S "--target=${target}" "${source}" -o "${assembly}"
                RESULT_VARIABLE assembly_status
                OUTPUT_QUIET ERROR_QUIET)

            set(instructions 0)
            if(assembly_status EQUAL 0 AND EXISTS "${assembly}")
                file(READ "${assembly}" text)
                # One instruction per indented mnemonic line; directives and labels are
                # not instructions and start with '.' or a non-indented name.
                string(REGEX MATCHALL "\n[ \t]+[A-Z][A-Z0-9]*" mnemonics "${text}")
                list(LENGTH mnemonics instructions)
            endif()
            file(APPEND "${OUT}" "${label}\t${target}\t${object_bytes}\t${instructions}\tok\n")
        else()
            # A rejection is a result, not a failure: the KR16 profile refuses some
            # sources, and recording that keeps the row count stable across runs.
            file(APPEND "${OUT}" "${label}\t${target}\t-\t-\trejected\n")
        endif()
    endforeach()
endforeach()

message(STATUS "size baseline written to ${OUT}")
