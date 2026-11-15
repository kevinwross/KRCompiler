# Runs a linked KR16 image under the sibling VM and checks its exit status.
#
# The KR16 `vm` command reports the program's exit status as its own, so the
# check is the same shape as the KR32 runner. A link-only test cannot catch a
# defect in generated code that the program only shows when executed.
#
# Required: VM, IMAGE, EXPECT_EXIT
# Optional: FS_ROOT -- the directory the VM's file services see as the root;
#           FS_QUOTA -- the quota for that root.
#           Without it the VM sandboxes to ./sandbox and every open fails,
#           which reads exactly like a broken file layer.
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

set(VM_ARGS)
if(DEFINED FS_ROOT AND NOT FS_ROOT STREQUAL "")
    list(APPEND VM_ARGS --fs-root "${FS_ROOT}")
endif()
# The VM's filesystem quota defaults to 5 MiB and counts the whole root
# recursively, so a root that holds a build tree exhausts it and every write
# returns SYS_ENOSPC. A test that transfers bytes raises it explicitly.
if(DEFINED FS_QUOTA AND NOT FS_QUOTA STREQUAL "")
    list(APPEND VM_ARGS --fs-quota "${FS_QUOTA}")
endif()

execute_process(
    COMMAND "${VM}" vm "${IMAGE}" --max-cycles ${MAX_CYCLES} ${VM_ARGS}
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
