# Asserts which source lines of one file carry an address in an image's KDBG line
# table: ABSENT_LINES must have no `Line` record and PRESENT_LINES must have one.
#
# This is the invariant docs/symbols.md states: a `Line` record is written only for
# an instruction that carries the location, so a line whose instructions a pass
# removed has no record, and the format never maps it onto a neighbour. A test that
# names both halves pins the policy -- an emitter or pass change that starts
# recording a folded line, or stops recording a live one, fails here.
#
# The source is matched by the tail of the path KDBG recorded, because the record's
# source id depends on link order rather than on anything the test controls.
#
# Required: KRC, IMAGE, SOURCE, ABSENT_LINES, PRESENT_LINES (comma-separated line
# numbers; a `;` would split the argument before the script saw it).

if(NOT DEFINED KRC OR NOT DEFINED IMAGE OR NOT DEFINED SOURCE)
    message(FATAL_ERROR "CheckLineRecords.cmake requires KRC, IMAGE, SOURCE")
endif()
if(NOT DEFINED ABSENT_LINES)
    set(ABSENT_LINES "")
endif()
if(NOT DEFINED PRESENT_LINES)
    set(PRESENT_LINES "")
endif()
string(REPLACE "," ";" absent_lines "${ABSENT_LINES}")
string(REPLACE "," ";" present_lines "${PRESENT_LINES}")
if(absent_lines STREQUAL "" AND present_lines STREQUAL "")
    message(FATAL_ERROR "CheckLineRecords.cmake requires ABSENT_LINES or PRESENT_LINES")
endif()

execute_process(COMMAND "${KRC}" --dump-symbols "${IMAGE}"
                OUTPUT_VARIABLE dump
                ERROR_VARIABLE dump_error
                RESULT_VARIABLE dump_result)
if(NOT dump_result EQUAL 0)
    message(FATAL_ERROR "krc --dump-symbols failed: ${dump_error}")
endif()

# `source <id> <path>` is the record that names a file; the id is what `line`
# records refer to.
string(REPLACE "\n" ";" dump_records "${dump}")
set(source_id "")
foreach(record IN LISTS dump_records)
    if(record MATCHES "^source ([0-9]+) (.*)$")
        set(record_id "${CMAKE_MATCH_1}")
        set(record_path "${CMAKE_MATCH_2}")
        string(LENGTH "${SOURCE}" source_length)
        string(LENGTH "${record_path}" path_length)
        if(${path_length} GREATER_EQUAL ${source_length})
            math(EXPR tail_start "${path_length} - ${source_length}")
            string(SUBSTRING "${record_path}" ${tail_start} ${source_length} path_tail)
            if(path_tail STREQUAL "${SOURCE}")
                set(source_id "${record_id}")
            endif()
        endif()
    endif()
endforeach()
if(source_id STREQUAL "")
    message(FATAL_ERROR "${SOURCE} is not one of the image's sources:\n${dump}")
endif()

foreach(line IN LISTS absent_lines)
    string(FIND "${dump}" "source=${source_id} line=${line} column=" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR
                "line ${line} of ${SOURCE} has an address, but the policy says a line "
                "with no instruction must have no record\nimage: ${IMAGE}")
    endif()
endforeach()

foreach(line IN LISTS present_lines)
    string(FIND "${dump}" "source=${source_id} line=${line} column=" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
                "line ${line} of ${SOURCE} has no address, but the policy says a line "
                "whose instruction survives must have one\nimage: ${IMAGE}")
    endif()
endforeach()
