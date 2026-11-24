# Rebuilds a target's listing from its runtime source and compares it with the
# committed one, byte for byte.
#
#   cmake -DKRC=<krc> -DTARGET=kr32 -DSOURCE=<abs>/crt0.s
#         -DEXPECT=<abs>/kr32-crt0.lst -DWORK=<dir> -P CheckListingDrift.cmake
#
# The source is copied into the work directory and named relatively, so the
# listing's title is the same on every host and the comparison can be byte for byte
# rather than line by line with an exception for a path.

foreach(variable KRC TARGET SOURCE EXPECT WORK)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "CheckListingDrift: ${variable} is required")
    endif()
endforeach()
if(NOT EXISTS "${EXPECT}")
    message(FATAL_ERROR "CheckListingDrift: no committed listing at ${EXPECT}")
endif()

file(MAKE_DIRECTORY "${WORK}")
get_filename_component(source_name "${SOURCE}" NAME)
get_filename_component(source_stem "${SOURCE}" NAME_WE)
configure_file("${SOURCE}" "${WORK}/${source_name}" COPYONLY)

execute_process(
    COMMAND "${KRC}" -l "--target=${TARGET}" "${source_name}"
    WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE state
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(NOT state EQUAL 0)
    message(FATAL_ERROR "krc -l ${source_name} for ${TARGET} failed (${state}):\n${output}${error}")
endif()

set(rebuilt "${WORK}/${source_stem}.lst")
if(NOT EXISTS "${rebuilt}")
    message(FATAL_ERROR "krc -l wrote no ${source_stem}.lst for ${TARGET}")
endif()

file(READ "${rebuilt}" actual)
file(READ "${EXPECT}" expected)
if(NOT actual STREQUAL expected)
    message(FATAL_ERROR
        "${TARGET}'s listing does not match ${EXPECT}.\n"
        "A listing is the machine's own description of its bytes: if this change was "
        "intended, regenerate the committed listing and say why in the commit.\n"
        "--- committed ---\n${expected}\n--- rebuilt ---\n${actual}")
endif()
message(STATUS "CheckListingDrift: ${TARGET} matches ${EXPECT}")
