# Runs a linked kr232 image under the sibling emulator and checks its exit status.
#
# A link-only test cannot catch a defect in generated code that the program only
# shows when executed, so this rule launches `kr232-emu vm IMAGE`. The emulator
# returns the program's `SYS exit` status as its own process status, which is the
# contract kr232-plan §6 fixes and the reason the check reads the process code.
#
# Required: VM, IMAGE, EXPECT_EXIT
# Optional: MAX_STEPS (default 200000), EXPECT_OUTPUT (a regular expression
#           matched against stdout)

if(NOT DEFINED VM OR NOT DEFINED IMAGE OR NOT DEFINED EXPECT_EXIT)
    message(FATAL_ERROR "VM, IMAGE, and EXPECT_EXIT are required")
endif()
if(NOT DEFINED MAX_STEPS)
    set(MAX_STEPS 200000)
endif()
if(NOT EXISTS "${VM}")
    message(FATAL_ERROR "VM not found: ${VM}")
endif()
if(NOT EXISTS "${IMAGE}")
    message(FATAL_ERROR "image not found: ${IMAGE}")
endif()

execute_process(
    COMMAND "${VM}" vm "${IMAGE}" --max-steps ${MAX_STEPS}
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
