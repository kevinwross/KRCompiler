# Checks a compiled kr232 image's KDBG payload for the invariants a debugger relies
# on: the target id is 5, the payload names at least one line and one function, and
# every line address falls inside the span of the functions.
#
# The work directory belongs to the row that owns it: create it rather than
# assuming the shared test output directory already exists.
file(MAKE_DIRECTORY "${WORK}")
 
# Required: KRC, KRLD, CRT0, SOURCE, WORK

foreach(required KRC KRLD CRT0 SOURCE WORK)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

execute_process(COMMAND "${KRC}" -c --target kr232 "${CRT0}" -o "${WORK}/kdbg_crt0.o"
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "assembling crt0 failed: ${output}${error}")
endif()

execute_process(COMMAND "${KRC}" -c --target kr232 "${SOURCE}" -o "${WORK}/kdbg_program.o"
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "compiling failed: ${output}${error}")
endif()
execute_process(COMMAND "${KRLD}" --entry=_start
                        "${WORK}/kdbg_crt0.o" "${WORK}/kdbg_program.o"
                        -o "${WORK}/kdbg.kr232"
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "linking failed: ${output}${error}")
endif()
execute_process(COMMAND "${KRC}" --dump-symbols "${WORK}/kdbg.kr232"
                RESULT_VARIABLE result OUTPUT_VARIABLE dump ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "the dump failed: ${output}${error}")
endif()

if(NOT dump MATCHES "KDBG target=5")
    message(FATAL_ERROR "the payload does not name target 5:\n${dump}")
endif()
if(NOT dump MATCHES "functions=([1-9][0-9]*)")
    message(FATAL_ERROR "the payload names no function:\n${dump}")
endif()
if(NOT dump MATCHES "lines=([1-9][0-9]*)")
    message(FATAL_ERROR "the payload names no line:\n${dump}")
endif()

# Every line address must sit inside some function's span, or a breakpoint on it
# would land outside the code the debugger knows about.
string(REGEX MATCHALL "function [^ \n]+ 0x([0-9a-f]+)-0x([0-9a-f]+)" functions "${dump}")
if(functions STREQUAL "")
    message(FATAL_ERROR "no function spans:\n${dump}")
endif()
set(lowest "")
set(highest "")
foreach(entry ${functions})
    string(REGEX REPLACE ".*0x([0-9a-f]+)-0x([0-9a-f]+).*" "\\1" first "${entry}")
    string(REGEX REPLACE ".*0x([0-9a-f]+)-0x([0-9a-f]+).*" "\\2" last "${entry}")
    if(first LESS lowest)
        set(lowest ${first})
    endif()
    if(last GREATER highest)
        set(highest ${last})
    endif()
endforeach()
string(REGEX MATCHALL "line 0x([0-9a-f]+)" line_records "${dump}")
foreach(record ${line_records})
    string(REGEX REPLACE "line 0x([0-9a-f]+)" "\\1" address "${record}")
    if(address LESS lowest OR address GREATER highest)
        message(FATAL_ERROR "line 0x${address} is outside 0x${lowest}-0x${highest}")
    endif()
endforeach()
message(STATUS "the payload names ${functions} and stays inside its code")
