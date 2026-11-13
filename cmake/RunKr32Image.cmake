# Runs a linked KR32 image under the sibling VM and checks its exit status.
#
# A link-only test cannot catch a defect in generated code that the program
# only shows when executed, which is how a truncated 64-bit cast survived the
# suite. When no VM is configured the caller does not register these tests.
#
# Required: VM, IMAGE, EXPECT_EXIT
# Optional: MAX_STEPS (default 20000000)

if(NOT DEFINED VM OR NOT DEFINED IMAGE OR NOT DEFINED EXPECT_EXIT)
    message(FATAL_ERROR "VM, IMAGE, and EXPECT_EXIT are required")
endif()
if(NOT DEFINED MAX_STEPS)
    set(MAX_STEPS 20000000)
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
