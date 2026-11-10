if(NOT DEFINED EXE)
    message(FATAL_ERROR "EXE is required")
endif()

execute_process(
    COMMAND "${EXE}" --target=kr32 -fsyntax-only does-not-exist.c
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    RESULT_VARIABLE result
)

if(NOT result EQUAL 1)
    message(FATAL_ERROR "expected exit 1 for a missing input, got ${result}")
endif()
if(NOT error MATCHES "unable to open")
    message(FATAL_ERROR "missing-file diagnostic not found: ${error}")
endif()
