# Links a fixture and checks that its debug dump shows the expected display
# names. The dump comes from the linked image because the linker is what turns
# the object's `.debug_info` records into KDBG.
#
# Required: KRC, KRLD, SOURCE, LIBC, IO, EH, RUNTIME, OUTPUT, EXPECT
#   EXPECT is a `;`-separated list of substrings that must appear.

if(NOT DEFINED KRC OR NOT DEFINED KRLD OR NOT DEFINED SOURCE OR NOT DEFINED LIBC OR
   NOT DEFINED IO OR NOT DEFINED EH OR NOT DEFINED RUNTIME OR NOT DEFINED OUTPUT OR
   NOT DEFINED EXPECT)
    message(FATAL_ERROR
            "CheckDebugNames.cmake requires KRC, KRLD, SOURCE, LIBC, IO, EH, RUNTIME, OUTPUT, EXPECT")
endif()

set(object "${OUTPUT}.o.kro")
file(REMOVE "${object}" "${OUTPUT}")

execute_process(COMMAND "${KRC}" -c "${SOURCE}" -o "${object}"
                RESULT_VARIABLE compile_result)
if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR "krc failed to compile ${SOURCE}")
endif()

execute_process(COMMAND "${KRLD}" "${object}" "${LIBC}" "${IO}" "${EH}" "${RUNTIME}"
                        -o "${OUTPUT}"
                RESULT_VARIABLE link_result)
if(NOT link_result EQUAL 0)
    message(FATAL_ERROR "krld failed to link ${OUTPUT}")
endif()

execute_process(COMMAND "${KRC}" --dump-symbols "${OUTPUT}"
                OUTPUT_VARIABLE dump
                ERROR_VARIABLE dump_error
                RESULT_VARIABLE dump_result)
if(NOT dump_result EQUAL 0)
    message(FATAL_ERROR "krc --dump-symbols failed: ${dump_error}")
endif()

foreach(expected IN LISTS EXPECT)
    string(FIND "${dump}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "expected '${expected}' in the debug dump of ${OUTPUT}")
    endif()
endforeach()
