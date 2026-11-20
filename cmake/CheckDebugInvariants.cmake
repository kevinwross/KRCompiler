# Asserts the two invariants docs/plan-o1-debugger.md W3 names over a linked image:
#
#  1. every `Line` address lies inside some function's `start`-`end`;
#  2. every `Variable`'s frame offset plus its size lies inside the frame the
#     `Function` record claims.
#
# Both are what a debugger needs to be honest: a breakpoint that places outside any
# function, or a variable that reads past its frame, is a wrong answer rather than a
# missing one. The image is expected to have been built at `-O1`, where frames are
# smaller and lines are fewer, so the invariants are checked on the harder case.
#
# Required: KRC, IMAGE.
#
# The line sweep is O(lines + functions) rather than O(lines * functions): the
# function ranges and the line addresses are each sorted once, then one walk advances
# a function index that never moves backwards. A per-address scan of 211 ranges would
# be 2.1 million comparisons in a CMake loop, which is minutes rather than seconds.

if(NOT DEFINED KRC OR NOT DEFINED IMAGE)
    message(FATAL_ERROR "CheckDebugInvariants.cmake requires KRC, IMAGE")
endif()
if(NOT EXISTS "${IMAGE}")
    message(FATAL_ERROR "CheckDebugInvariants.cmake: no image at ${IMAGE}")
endif()

execute_process(COMMAND "${KRC}" --dump-symbols "${IMAGE}"
                OUTPUT_VARIABLE dump
                ERROR_VARIABLE dump_error
                RESULT_VARIABLE dump_result)
if(NOT dump_result EQUAL 0)
    message(FATAL_ERROR "krc --dump-symbols failed: ${dump_error}")
endif()

string(REPLACE "\n" ";" records "${dump}")
set(ranges "")
set(function_names "")
set(function_frames "")
set(addresses "")
set(variables "")

foreach(record IN LISTS records)
    if(record MATCHES "^function (.+) (0x[0-9a-fA-F]+)-(0x[0-9a-fA-F]+) frame=([0-9]+) type=")
        math(EXPR start "${CMAKE_MATCH_2}")
        math(EXPR finish "${CMAKE_MATCH_3}")
        list(APPEND ranges "${start}|${finish}")
        list(APPEND function_names "${CMAKE_MATCH_1}")
        list(APPEND function_frames "${CMAKE_MATCH_4}")
    elseif(record MATCHES "^line (0x[0-9a-fA-F]+) source=")
        math(EXPR address "${CMAKE_MATCH_1}")
        list(APPEND addresses "${address}")
    elseif(record MATCHES "^variable (.+)\\.[^.]+ offset=([0-9]+) type=.+ size=([0-9]+)")
        list(APPEND variables "${CMAKE_MATCH_1}|${CMAKE_MATCH_2}|${CMAKE_MATCH_3}")
    endif()
endforeach()

list(LENGTH ranges function_count)
list(LENGTH addresses line_count)
list(LENGTH variables variable_count)
if(function_count EQUAL 0 OR line_count EQUAL 0)
    message(FATAL_ERROR "${IMAGE} has no functions or no lines:\n${dump}")
endif()

# Invariant 1: line addresses inside a function.
list(SORT ranges COMPARE NATURAL ORDER ASCENDING)
list(SORT addresses COMPARE NATURAL ORDER ASCENDING)

set(starts "")
set(ends "")
foreach(entry IN LISTS ranges)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 start)
    list(GET parts 1 finish)
    list(APPEND starts "${start}")
    list(APPEND ends "${finish}")
endforeach()

math(EXPR last "${function_count} - 1")
set(index 0)
set(outside "")
set(outside_count 0)
foreach(address IN LISTS addresses)
    while(index LESS last)
        math(EXPR next "${index} + 1")
        list(GET starts ${next} next_start)
        if(${next_start} GREATER ${address})
            break()
        endif()
        set(index ${next})
    endwhile()
    list(GET starts ${index} start)
    list(GET ends ${index} finish)
    if(${address} LESS ${start} OR NOT ${address} LESS ${finish})
        math(EXPR outside_count "${outside_count} + 1")
        list(LENGTH outside outside_length)
        if(outside_length LESS 5)
            list(APPEND outside "${address} (nearest function starts at ${start}, ends before ${finish})")
        endif()
    endif()
endforeach()
if(outside_count GREATER 0)
    string(REPLACE ";" "\n  " outside_text "${outside}")
    message(FATAL_ERROR
            "${outside_count} of ${line_count} line addresses fall outside every function "
            "in ${IMAGE}:\n  ${outside_text}")
endif()

# Invariant 2: a variable's offset plus its size inside the frame its function claims.
set(overrun "")
set(overrun_count 0)
foreach(variable IN LISTS variables)
    string(REPLACE "|" ";" parts "${variable}")
    list(GET parts 0 name)
    list(GET parts 1 offset)
    list(GET parts 2 size)
    list(FIND function_names "${name}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
                "variable ${name} names a function with no record in ${IMAGE}, so its "
                "frame cannot be checked")
    endif()
    list(GET function_frames ${position} frame)
    math(EXPR extent "${offset} + ${size}")
    if(${extent} GREATER ${frame})
        math(EXPR overrun_count "${overrun_count} + 1")
        list(LENGTH overrun overrun_length)
        if(overrun_length LESS 5)
            list(APPEND overrun "${name} needs ${extent} bytes of a ${frame}-byte frame")
        endif()
    endif()
endforeach()
if(overrun_count GREATER 0)
    string(REPLACE ";" "\n  " overrun_text "${overrun}")
    message(FATAL_ERROR
            "${overrun_count} of ${variable_count} variables reach outside their frame in "
            "${IMAGE}:\n  ${overrun_text}")
endif()

message(STATUS
        "invariants hold: ${line_count} lines inside ${function_count} functions, "
        "${variable_count} variables inside their frames")
