# Builds a kr232 program from a C fixture and the runtime, then runs it.
#
# The pipeline is the one the milestone's checks name: `krc -c` for the runtime
# and for the program, `krld --entry=_start` to link them, and the emulator to run
# the result. Doing all three in one script keeps the test's evidence in one place
# rather than splitting it across tests whose order CTest does not guarantee.
#
# Required: KRC, KRLD, VM, CRT0, SOURCE, WORK, EXPECT_EXIT

set(include_args)
if(DEFINED INCLUDE AND NOT INCLUDE STREQUAL "")
    set(include_args -I "${INCLUDE}")
endif()

# An object whose calls leave it cannot have them widened afterwards, so the
# compiler emits the wide form for every call when asked.
set(far_args)
if(DEFINED FAR_CALLS AND FAR_CALLS)
    set(far_args --far-calls)
endif()

# The work directory belongs to the caller and may now be a row's own: create it
# rather than assuming the shared test output directory already exists.
file(MAKE_DIRECTORY "${WORK}")

foreach(required KRC KRLD VM CRT0 SOURCE WORK)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

execute_process(COMMAND "${KRC}" -c --target kr232 ${far_args} "${CRT0}"
                        -o "${WORK}/kr232_crt0.o"
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "assembling crt0 failed: ${output}${error}")
endif()

execute_process(COMMAND "${KRC}" -c --target kr232 ${far_args} ${include_args} "${SOURCE}"
                        -o "${WORK}/kr232_program.o"
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "compiling ${SOURCE} failed: ${output}${error}")
endif()

set(link_inputs "${WORK}/kr232_crt0.o" "${WORK}/kr232_program.o")
if(DEFINED PACKAGES AND NOT PACKAGES STREQUAL "")
    # The installed packages, as the prefix ships them: a program that includes
    # <kr232/cons.h> links these objects, exactly as the debugger's build chain
    # does.
    file(GLOB _package_objects "${PACKAGES}/*.kro")
    if(_package_objects STREQUAL "")
        message(FATAL_ERROR "no package objects in ${PACKAGES}")
    endif()
    list(APPEND link_inputs ${_package_objects})
endif()
if(DEFINED LIBC AND NOT LIBC STREQUAL "")
    execute_process(COMMAND "${KRC}" -c --target kr232 ${far_args} ${include_args} "${LIBC}"
                            -o "${WORK}/kr232_libc.o"
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "compiling ${LIBC} failed: ${output}${error}")
    endif()
    list(APPEND link_inputs "${WORK}/kr232_libc.o")
endif()

if(DEFINED EXTRA AND NOT EXTRA STREQUAL "")
    # Further sources the program needs: the host layer of a C runtime, or an
    # object a fixture is built from.
    foreach(extra IN LISTS EXTRA)
        get_filename_component(extra_name "${extra}" NAME_WE)
        execute_process(COMMAND "${KRC}" -c --target kr232 ${far_args} ${include_args}
                                "${extra}" -o "${WORK}/${extra_name}.o"
                        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "compiling ${extra} failed: ${output}${error}")
        endif()
        list(APPEND link_inputs "${WORK}/${extra_name}.o")
    endforeach()
endif()

execute_process(COMMAND "${KRLD}" --entry=_start ${link_inputs}
                        -o "${WORK}/kr232_program.kr232"
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "linking failed: ${output}${error}")
endif()

# The terminal transcript is the console's observable output: `--transcript`
# prints the grid, and EXPECT_TRANSCRIPT is a substring the grid must contain.
set(run_args)
if(DEFINED EXPECT_TRANSCRIPT AND NOT EXPECT_TRANSCRIPT STREQUAL "")
    list(APPEND run_args --transcript)
endif()
if(DEFINED EXPECT_FRAME AND NOT EXPECT_FRAME STREQUAL "")
    list(APPEND run_args --frame-hash)
endif()

execute_process(COMMAND "${VM}" vm "${WORK}/kr232_program.kr232" --max-steps 200000
                        ${run_args}
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(DEFINED EXPECT_EXIT AND NOT EXPECT_EXIT STREQUAL "" AND NOT result EQUAL EXPECT_EXIT)
    message(FATAL_ERROR "expected exit ${EXPECT_EXIT}, got ${result}\n${output}${error}")

# The machine's own status is the full 32-bit value; a POSIX process status keeps
# only its low eight bits, so a program exiting with 300 reads back as 44 there.
if(DEFINED EXPECT_STATUS AND NOT EXPECT_STATUS STREQUAL "")
    if(NOT output MATCHES "halted status ([0-9]+)")
        message(FATAL_ERROR "no halted status in the output:\n${output}${error}")
    endif()
    if(NOT CMAKE_MATCH_1 STREQUAL EXPECT_STATUS)
        message(FATAL_ERROR
            "expected machine status ${EXPECT_STATUS}, got ${CMAKE_MATCH_1}\n${output}")
    endif()
endif()
endif()
if(DEFINED EXPECT_FRAME AND NOT EXPECT_FRAME STREQUAL "")
    if(NOT output MATCHES "frame ${EXPECT_FRAME}")
    # A drawing golden that equals the frame a program commits without drawing
    # anything is not a drawing: two were recorded that way when the graphics
    # clip clipped every pixel away and the device still answered OK.
    if(DEFINED BLANK_IMAGE AND NOT BLANK_IMAGE STREQUAL "" AND EXISTS "${BLANK_IMAGE}")
        execute_process(COMMAND "${VM}" vm "${BLANK_IMAGE}" --frame-hash
                        OUTPUT_VARIABLE blank_output RESULT_VARIABLE blank_code)
        if(NOT blank_code EQUAL 0 OR NOT blank_output MATCHES "frame ([0-9a-f]+)")
            message(FATAL_ERROR "cannot measure the blank frame: ${blank_output}")
        endif()
        if(CMAKE_MATCH_1 STREQUAL EXPECT_FRAME)
            message(FATAL_ERROR
                "the golden ${EXPECT_FRAME} is the blank committed frame: nothing was drawn")
        endif()
    endif()
        message(FATAL_ERROR "expected frame ${EXPECT_FRAME}, got:\n${output}${error}")
    endif()
endif()
if(DEFINED EXPECT_TRANSCRIPT AND NOT EXPECT_TRANSCRIPT STREQUAL "")
    if(NOT output MATCHES "${EXPECT_TRANSCRIPT}")
        message(FATAL_ERROR
            "expected transcript matching '${EXPECT_TRANSCRIPT}', got:\n${output}${error}")
    endif()
endif()
message(STATUS "the program exited ${result} as expected")
