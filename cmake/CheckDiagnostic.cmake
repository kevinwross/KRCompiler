# Runs the compiler over a fixture that must be rejected, and checks that the
# expected diagnostic text appears. A compiler that accepts the fixture exits 0
# and fails this check.
#
# Required: EXE, SOURCE, EXPECT (a regular expression matched against stderr)
# Optional: TARGET (defaults to kr32)

if(NOT DEFINED EXE OR NOT DEFINED SOURCE OR NOT DEFINED EXPECT)
    message(FATAL_ERROR "EXE, SOURCE, and EXPECT are required")
endif()
if(NOT DEFINED TARGET)
    set(TARGET kr32)
endif()

execute_process(
    COMMAND "${EXE}" "--target=${TARGET}" -fsyntax-only "${SOURCE}"
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    RESULT_VARIABLE result)

if(result EQUAL 0)
    message(FATAL_ERROR "expected a diagnostic, but the compiler accepted ${SOURCE}")
endif()
if(NOT error MATCHES "${EXPECT}")
    message(FATAL_ERROR "expected '${EXPECT}' in the diagnostics, got: ${error}${output}")
endif()
