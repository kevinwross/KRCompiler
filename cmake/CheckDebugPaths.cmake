if(NOT DEFINED KRC OR NOT DEFINED KRLD OR NOT DEFINED SOURCE OR
   NOT DEFINED LIBC OR NOT DEFINED IO OR NOT DEFINED CRT0 OR
   NOT DEFINED ROOT OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR
            "CheckDebugPaths.cmake requires KRC, KRLD, SOURCE, LIBC, IO, CRT0, ROOT, OUTPUT")
endif()

set(object "${OUTPUT}.o.kro")
file(REMOVE "${object}" "${OUTPUT}")

execute_process(COMMAND "${KRC}" -c "${SOURCE}" -o "${object}"
                RESULT_VARIABLE compile_result)
if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR "krc failed to compile the debug-path fixture")
endif()

execute_process(COMMAND "${KRLD}" "${object}" "${LIBC}" "${IO}" "${CRT0}"
                        "--source-root=${ROOT}" -o "${OUTPUT}"
                RESULT_VARIABLE link_result)
if(NOT link_result EQUAL 0)
    message(FATAL_ERROR "krld failed to link the debug-path fixture")
endif()

execute_process(COMMAND "${KRC}" --dump-symbols "${OUTPUT}"
                RESULT_VARIABLE dump_result
                OUTPUT_VARIABLE dump
                ERROR_VARIABLE dump_error)
if(NOT dump_result EQUAL 0)
    message(FATAL_ERROR "krc --dump-symbols failed: ${dump_error}")
endif()

string(FIND "${dump}" "${ROOT}" root_position)
if(NOT root_position EQUAL -1)
    message(FATAL_ERROR "KDBG retained the source root: ${dump}")
endif()

string(FIND "${dump}" "tests/fixtures/c/hello.c" relative_position)
if(relative_position EQUAL -1)
    message(FATAL_ERROR "KDBG did not record the relative source path: ${dump}")
endif()
