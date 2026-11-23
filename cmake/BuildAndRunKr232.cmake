# Builds a kr232 program from a C fixture and the runtime, then runs it.
#
# The pipeline is the one the milestone's checks name: `krc -c` for the runtime
# and for the program, `krld --entry=_start` to link them, and the emulator to run
# the result. Doing all three in one script keeps the test's evidence in one place
# rather than splitting it across tests whose order CTest does not guarantee.
#
# Required: KRC, KRLD, VM, CRT0, SOURCE, WORK, EXPECT_EXIT

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

execute_process(COMMAND "${KRC}" -c --target kr232 "${SOURCE}"
                        -o "${WORK}/kr232_program.o"
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "compiling ${SOURCE} failed: ${output}${error}")
endif()

execute_process(COMMAND "${KRLD}" --entry=_start
                        "${WORK}/kr232_crt0.o" "${WORK}/kr232_program.o"
                        -o "${WORK}/kr232_program.k232"
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "linking failed: ${output}${error}")
endif()

execute_process(COMMAND "${VM}" vm "${WORK}/kr232_program.k232" --max-steps 200000
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL EXPECT_EXIT)
    message(FATAL_ERROR "expected exit ${EXPECT_EXIT}, got ${result}\n${output}${error}")
endif()
message(STATUS "the program exited ${result} as expected")
