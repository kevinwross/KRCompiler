# Runs a command and requires it to fail with a message, which CTest cannot
# express directly: WILL_FAIL inverts PASS_REGULAR_EXPRESSION as well as the exit
# code, so a row written that way fails exactly when the message appears.
#
#   cmake -DEXE=<krc> "-DARGS=image;--target=kr32;..." -DEXPECT=says kr16 -P expect_refusal.cmake
cmake_minimum_required(VERSION 3.25)
if(NOT DEFINED EXE OR NOT DEFINED ARGS OR NOT DEFINED EXPECT)
    message(FATAL_ERROR "pass -DEXE, -DARGS and -DEXPECT")
endif()
execute_process(COMMAND "${EXE}" ${ARGS}
                RESULT_VARIABLE status
                OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0)
    message(FATAL_ERROR "${EXE} ${ARGS} succeeded; it was expected to refuse")
endif()
set(combined "${out}${err}")
string(FIND "${combined}" "${EXPECT}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "refusal did not mention '${EXPECT}': ${combined}")
endif()
message(STATUS "refused as expected: ${combined}")
