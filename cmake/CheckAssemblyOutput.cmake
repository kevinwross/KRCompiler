# Checks that `krc -S -o FILE` writes the assembly to FILE rather than to stdout.
# The driver accepted the combination and silently kept printing to stdout,
# which made a build that captured stdout collect nothing.
#
# Required: EXE, SOURCE, OUT
# Optional: TARGET (defaults to kr32), EXPECT (a regular expression that the
#           written assembly must match)

if(NOT DEFINED EXE OR NOT DEFINED SOURCE OR NOT DEFINED OUT)
    message(FATAL_ERROR "EXE, SOURCE, and OUT are required")
endif()
if(NOT DEFINED TARGET)
    set(TARGET kr32)
endif()

file(REMOVE "${OUT}")
execute_process(
    COMMAND "${EXE}" "--target=${TARGET}" -S "${SOURCE}" -o "${OUT}"
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE error
    RESULT_VARIABLE result)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "krc -S -o failed with ${result}: ${error}${stdout}")
endif()
if(NOT EXISTS "${OUT}")
    message(FATAL_ERROR "-S -o did not create ${OUT}")
endif()
file(READ "${OUT}" written)
if(written STREQUAL "")
    message(FATAL_ERROR "${OUT} is empty")
endif()
if(DEFINED EXPECT AND NOT EXPECT STREQUAL "" AND NOT written MATCHES "${EXPECT}")
    message(FATAL_ERROR "${OUT} does not match '${EXPECT}': ${written}")
endif()
