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

foreach(required KRC KRLD VM CRT0 SOURCE WORK EXPECT_EXIT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

execute_process(COMMAND "${KRC}" -c --target kr232 "${CRT0}"
                        -o "${WORK}/kr232_crt0.o"
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "assembling crt0 failed: ${output}${error}")
endif()

execute_process(COMMAND "${KRC}" -c --target kr232 ${include_args} "${SOURCE}"
                        -o "${WORK}/kr232_program.o"
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "compiling ${SOURCE} failed: ${output}${error}")
endif()

set(link_inputs "${WORK}/kr232_crt0.o" "${WORK}/kr232_program.o")
if(DEFINED LIBC AND NOT LIBC STREQUAL "")
    execute_process(COMMAND "${KRC}" -c --target kr232 ${include_args} "${LIBC}"
                            -o "${WORK}/kr232_libc.o"
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "compiling ${LIBC} failed: ${output}${error}")
    endif()
    list(APPEND link_inputs "${WORK}/kr232_libc.o")
endif()

execute_process(COMMAND "${KRLD}" --entry=_start ${link_inputs}
                        -o "${WORK}/kr232_program.k232"
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

execute_process(COMMAND "${VM}" vm "${WORK}/kr232_program.k232" --max-steps 200000
                        ${run_args}
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL EXPECT_EXIT)
    message(FATAL_ERROR "expected exit ${EXPECT_EXIT}, got ${result}\n${output}${error}")
endif()
if(DEFINED EXPECT_FRAME AND NOT EXPECT_FRAME STREQUAL "")
    if(NOT output MATCHES "frame ${EXPECT_FRAME}")
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
