# Builds and runs the multi-file startrekng example for one VM VM_TARGET.
#
# Required: KRC, KRLD, VM_TARGET, RUN_MAIN, SOURCE_DIR, WORK
# Optional: VM, EXPECT, EXPECT_EXIT, MAX_STEPS, INPUT

if(NOT DEFINED KRC OR NOT DEFINED KRLD OR NOT DEFINED VM_TARGET OR
   NOT DEFINED RUN_MAIN OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK)
    message(FATAL_ERROR
        "BuildAndRunStartrekng.cmake requires KRC, KRLD, VM_TARGET, RUN_MAIN, SOURCE_DIR, WORK")
endif()

set(sources console geometry galaxy ship scanner weapons computer game)
set(objects)
set(far_args)
set(level_args)
if(DEFINED LEVEL AND NOT LEVEL STREQUAL "")
    list(APPEND level_args ${LEVEL})
endif()
if(VM_TARGET STREQUAL "kr232")
    list(APPEND far_args --far-calls)
endif()

file(MAKE_DIRECTORY "${WORK}")
foreach(name IN LISTS sources)
    set(object "${WORK}/${name}.o")
    file(REMOVE "${object}")
    execute_process(
        COMMAND "${KRC}" -c ${far_args} ${level_args} --target=${VM_TARGET}
                "${SOURCE_DIR}/${name}.cpp" -o "${object}"
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "krc failed for ${name}.cpp with ${result}")
    endif()
    list(APPEND objects "${object}")
endforeach()

set(main_object "${WORK}/main.o")
file(REMOVE "${main_object}")
execute_process(
    COMMAND "${KRC}" -c ${far_args} ${level_args} --target=${VM_TARGET}
            "${SOURCE_DIR}/${RUN_MAIN}" -o "${main_object}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "krc failed for ${RUN_MAIN} with ${result}")
endif()
list(APPEND objects "${main_object}")

if(VM_TARGET STREQUAL "kr32")
    set(image "${WORK}/startrekng.kr32")
    if(NOT DEFINED KRC_STAGE_RUNTIME)
        message(FATAL_ERROR "KRC_STAGE_RUNTIME is required for kr32")
    endif()
    list(APPEND objects "${KRC_STAGE_RUNTIME}/libkrcrt.kro")
    list(APPEND objects "${SOURCE_DIR}/../../targets/kr32/runtime/fileio.s"
                        "${SOURCE_DIR}/../../targets/kr32/runtime/eh.s")
    set(link_prefix "${SOURCE_DIR}/../../targets/kr32/runtime/crt0.s")
    execute_process(
        COMMAND "${KRLD}" "${link_prefix}" ${objects} -o "${image}"
        RESULT_VARIABLE result)
elseif(VM_TARGET STREQUAL "kr232")
    set(image "${WORK}/startrekng.kr232")
    set(crt0 "${WORK}/crt0.o")
    set(libc "${WORK}/libc.o")
    set(fileio "${WORK}/fileio.o")
    execute_process(
        COMMAND "${KRC}" -c --far-calls --target=kr232
                "${SOURCE_DIR}/../../targets/kr232/runtime/crt0.s" -o "${crt0}"
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "krc failed for the kr232 crt0 with ${result}")
    endif()
    execute_process(
        COMMAND "${KRC}" -c --far-calls --target=kr232
                "${SOURCE_DIR}/../../targets/kr232/runtime/libc_full.c" -o "${libc}"
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "krc failed for the kr232 libc with ${result}")
    endif()
    execute_process(
        COMMAND "${KRC}" -c --far-calls --target=kr232
                "${SOURCE_DIR}/../../targets/kr232/runtime/fileio_full.s" -o "${fileio}"
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "krc failed for the kr232 fileio with ${result}")
    endif()
    execute_process(
        COMMAND "${KRLD}" --entry=_start "${crt0}" ${objects} "${libc}" "${fileio}"
                -o "${image}"
        RESULT_VARIABLE result)
else()
    message(FATAL_ERROR "unknown VM_TARGET '${VM_TARGET}'")
endif()

if(NOT result EQUAL 0)
    message(FATAL_ERROR "krld failed with ${result}")
endif()

if(NOT DEFINED VM OR VM STREQUAL "")
    return()
endif()

if(NOT DEFINED MAX_STEPS)
    set(MAX_STEPS 50000000)
endif()
set(run_args --max-steps "${MAX_STEPS}")
if(VM_TARGET STREQUAL "kr232")
    list(APPEND run_args --transcript)
endif()
if(DEFINED INPUT AND NOT INPUT STREQUAL "")
    list(APPEND run_args --input "${INPUT}")
endif()

execute_process(COMMAND "${VM}" vm "${image}" ${run_args}
                OUTPUT_VARIABLE output ERROR_VARIABLE errors
                RESULT_VARIABLE run_result)
if(DEFINED EXPECT_EXIT)
    if(NOT run_result EQUAL EXPECT_EXIT)
        message(FATAL_ERROR "expected exit ${EXPECT_EXIT}, got ${run_result}\n${output}${errors}")
    endif()
elseif(NOT run_result EQUAL 0)
    message(FATAL_ERROR "startrekng exited ${run_result}\n${output}${errors}")
endif()

if(DEFINED EXPECT AND NOT EXPECT STREQUAL "")
    string(FIND "${output}${errors}" "${EXPECT}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "missing transcript '${EXPECT}'\n${output}${errors}")
    endif()
endif()
