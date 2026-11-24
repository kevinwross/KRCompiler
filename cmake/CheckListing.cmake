# Runs a listing-producing command and asserts the file it wrote.
#
#   cmake -DKRC=<krc> -DWORK=<dir> -DSOURCE=<file> -DARGS=<;>-list
#         -DEXPECT_FILE=<path> -DEXPECT_TEXT=<regex> -P CheckListing.cmake
#
# A listing is a file, so the row checks the file: that the command succeeded,
# that the name D5 derives was the one written, and that the text carries what the
# listing is supposed to carry. A row that only checked the exit code would pass
# with an empty file.

if(NOT DEFINED KRC OR NOT DEFINED WORK OR NOT DEFINED SOURCE OR NOT DEFINED EXPECT_FILE)
    message(FATAL_ERROR "CheckListing: KRC, WORK, SOURCE and EXPECT_FILE are required")
endif()

file(MAKE_DIRECTORY "${WORK}")
file(REMOVE "${EXPECT_FILE}")

execute_process(
    COMMAND "${KRC}" ${ARGS} "${SOURCE}"
    WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE state
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)

if(NOT state EQUAL 0)
    message(FATAL_ERROR "listing command failed (${state}):\n${output}${error}")
endif()

if(NOT EXISTS "${EXPECT_FILE}")
    message(FATAL_ERROR "expected ${EXPECT_FILE}, which the command did not write")
endif()

file(READ "${EXPECT_FILE}" text)
if(NOT text MATCHES "${EXPECT_TEXT}")
    message(FATAL_ERROR
        "${EXPECT_FILE} does not match ${EXPECT_TEXT}:\n${text}")
endif()
