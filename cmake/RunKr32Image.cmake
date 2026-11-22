# Runs a linked KR32 image under the sibling VM and checks its exit status.
#
# A link-only test cannot catch a defect in generated code that the program
# only shows when executed, which is how a truncated 64-bit cast survived the
# suite. When no VM is configured the caller does not register these tests.
#
# Required: VM, IMAGE, EXPECT_EXIT
# Optional: MAX_STEPS (default 20000000), FS_ROOT (the host filesystem sandbox
#           root; without it the VM defaults to ./sandbox and an open fails),
#           EXPECT_OUTPUT (a regular expression matched against stdout)

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

set(vm_args)
if(DEFINED FS_ROOT AND NOT FS_ROOT STREQUAL "")
    list(APPEND vm_args --fs-root "${FS_ROOT}")
endif()
execute_process(
    COMMAND "${VM}" vm "${IMAGE}" --max-steps ${MAX_STEPS} ${vm_args}
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    RESULT_VARIABLE result)

if(NOT result EQUAL EXPECT_EXIT)
    message(FATAL_ERROR
        "expected exit ${EXPECT_EXIT}, got ${result}\n${output}${error}")
endif()

# A program can print the wrong value and still exit 0, which is how a
# truncated 64-bit conversion survived a link-only test.
if(DEFINED EXPECT_OUTPUT AND NOT EXPECT_OUTPUT STREQUAL "")
    string(REPLACE "\n" "|" flattened "${output}")
    if(NOT flattened MATCHES "${EXPECT_OUTPUT}")
        message(FATAL_ERROR
            "expected output matching '${EXPECT_OUTPUT}', got: ${flattened}")
    endif()
endif()
