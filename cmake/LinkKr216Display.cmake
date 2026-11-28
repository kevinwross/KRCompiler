# Builds a kr216 program that includes <kr216/display.h> and links the display runtime.
#
# The link is the marshalling evidence available without a VM in this tree: the fixture calls
# every wrapper, so krld succeeds only when the header, the C wrappers and the SYS 5 trampoline
# all agree, and the merged symbol payload must name the wrapper entry points. The kr216
# repository runs the same kind of program under its emulator, so this row stops at the link.
#
# Required: KRC, KRLD, RUNTIME, SOURCE, WORK
# Optional: EXPECT_SYMBOLS (a ;-separated list the symbol dump must contain)

foreach(required KRC KRLD RUNTIME SOURCE WORK)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
foreach(tool KRC KRLD)
    if(NOT EXISTS "${${tool}}")
        message(FATAL_ERROR "${tool} not found: ${${tool}}")
    endif()
endforeach()
if(NOT IS_DIRECTORY "${RUNTIME}")
    message(FATAL_ERROR "runtime directory not found: ${RUNTIME}")
endif()

file(MAKE_DIRECTORY "${WORK}")

function(compile_object source output)
    execute_process(
        COMMAND "${KRC}" -c --target=kr216 "${source}" -o "${output}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output_text ERROR_VARIABLE error_text)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "krc failed for ${source} (${result}):\n${output_text}${error_text}")
    endif()
endfunction()

set(objects)
foreach(name crt0.s syscalls.s eh.s libc.c display.c)
    compile_object("${RUNTIME}/${name}" "${WORK}/${name}.kro")
    list(APPEND objects "${WORK}/${name}.kro")
endforeach()
compile_object("${SOURCE}" "${WORK}/program.kro")
list(APPEND objects "${WORK}/program.kro")

set(image "${WORK}/display.kr216")
execute_process(
    COMMAND "${KRLD}" ${objects} -o "${image}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output_text ERROR_VARIABLE error_text)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "krld failed (${result}):\n${output_text}${error_text}")
endif()
if(NOT EXISTS "${image}")
    message(FATAL_ERROR "krld reported success but wrote no image at ${image}")
endif()

execute_process(
    COMMAND "${KRC}" --dump-symbols "${image}"
    RESULT_VARIABLE result OUTPUT_VARIABLE symbols ERROR_VARIABLE error_text)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "krc --dump-symbols failed (${result}):\n${error_text}")
endif()
if(DEFINED EXPECT_SYMBOLS AND NOT EXPECT_SYMBOLS STREQUAL "")
    foreach(symbol IN LISTS EXPECT_SYMBOLS)
        string(FIND "${symbols}" "${symbol}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "the linked image does not name '${symbol}':\n${symbols}")
        endif()
    endforeach()
endif()

message(STATUS "linked ${image} with the display runtime")
