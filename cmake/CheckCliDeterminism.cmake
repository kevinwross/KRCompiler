if(NOT DEFINED EXE)
    message(FATAL_ERROR "EXE is required")
endif()

execute_process(
    COMMAND "${EXE}" --version
    OUTPUT_VARIABLE first_output
    ERROR_VARIABLE first_error
    RESULT_VARIABLE first_result
)
execute_process(
    COMMAND "${EXE}" --version
    OUTPUT_VARIABLE second_output
    ERROR_VARIABLE second_error
    RESULT_VARIABLE second_result
)

if(NOT first_result EQUAL 0 OR NOT second_result EQUAL 0)
    message(FATAL_ERROR "version command failed: ${first_result}/${second_result}; ${first_error}${second_error}")
endif()
if(NOT first_output STREQUAL second_output)
    message(FATAL_ERROR "version output is not deterministic")
endif()
if(NOT first_error STREQUAL second_error)
    message(FATAL_ERROR "version stderr is not deterministic")
endif()
