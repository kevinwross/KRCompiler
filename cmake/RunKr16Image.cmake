# Runs a linked KR16 image under the sibling VM and checks its exit status.
#
# The KR16 `vm` command reports the program's exit status as its own, so the
# check is the same shape as the KR32 runner. A link-only test cannot catch a
# defect in generated code that the program only shows when executed.
#
# Required: VM, IMAGE, EXPECT_EXIT
# Optional: MAX_CYCLES (default 20000000), EXPECT_OUTPUT (a regular expression
#           matched against stdout; newlines become '|' before matching)

if(NOT DEFINED VM OR NOT DEFINED IMAGE OR NOT DEFINED EXPECT_EXIT)
    message(FATAL_ERROR "VM, IMAGE, and EXPECT_EXIT are required")
endif()
if(NOT DEFINED MAX_CYCLES)
    set(MAX_CYCLES 20000000)
endif()
if(NOT EXISTS "${VM}")
    message(FATAL_ERROR "VM not found: ${VM}")
endif()
if(NOT EXISTS "${IMAGE}")
    message(FATAL_ERROR "image not found: ${IMAGE}")
endif()

execute_process(
    COMMAND "${VM}" vm "${IMAGE}" --max-cycles ${MAX_CYCLES}
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    RESULT_VARIABLE result)

if(NOT result EQUAL EXPECT_EXIT)
    message(FATAL_ERROR
        "expected exit ${EXPECT_EXIT}, got ${result}\n${output}${error}")
endif()

if(DEFINED EXPECT_OUTPUT AND NOT EXPECT_OUTPUT STREQUAL "")
    string(REPLACE "\n" "|" flattened "${output}")
    if(NOT flattened MATCHES "${EXPECT_OUTPUT}")
        message(FATAL_ERROR
            "expected output matching '${EXPECT_OUTPUT}', got: ${flattened}")
    endif()
endif()
