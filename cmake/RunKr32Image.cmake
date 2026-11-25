# Runs a linked KR32 image under the sibling VM and checks its exit status.
#
# A link-only test cannot catch a defect in generated code that the program
# only shows when executed, which is how a truncated 64-bit cast survived the
# suite. When no VM is configured the caller does not register these tests.
#
# Required: VM, IMAGE, EXPECT_EXIT
# Optional: MAX_STEPS (default 20000000), FS_ROOT (the host filesystem sandbox
#           root; without it the VM defaults to ./sandbox and an open fails),
#           ARGS (a ;-separated list of guest arguments, each passed as --arg),
#           EXPECT_OUTPUT (a regular expression matched against stdout),
#           EXPECT_FILE (a host file the run must write, non-empty),
#           EXPECT_FILE_TEXT (a regular expression the file must match)

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
if(DEFINED ARGS AND NOT ARGS STREQUAL "")
    foreach(argument IN LISTS ARGS)
        list(APPEND vm_args --arg "${argument}")
    endforeach()
endif()
# A run that fails to write its file would pass on a stale one from an earlier
# invocation, so the expected path is removed before the program starts.
if(DEFINED EXPECT_FILE AND NOT EXPECT_FILE STREQUAL "")
    file(REMOVE "${EXPECT_FILE}")
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

# An exit status says nothing about a file the program was asked to write.
if(DEFINED EXPECT_FILE AND NOT EXPECT_FILE STREQUAL "")
    if(NOT EXISTS "${EXPECT_FILE}")
        message(FATAL_ERROR "expected file not written: ${EXPECT_FILE}")
    endif()
    file(SIZE "${EXPECT_FILE}" expected_file_size)
    if(expected_file_size EQUAL 0)
        message(FATAL_ERROR "expected file is empty: ${EXPECT_FILE}")
    endif()
    if(DEFINED EXPECT_FILE_TEXT AND NOT EXPECT_FILE_TEXT STREQUAL "")
        file(READ "${EXPECT_FILE}" expected_file_text)
        if(NOT expected_file_text MATCHES "${EXPECT_FILE_TEXT}")
            message(FATAL_ERROR
                "${EXPECT_FILE} does not match '${EXPECT_FILE_TEXT}':\n${expected_file_text}")
        endif()
    endif()
endif()
