# Compiles one source twice and fails if the emitted object or image differs.
#
# Required: KRC, SOURCE, OUT1, OUT2
# Optional: KRC_TARGET (kr32 is the default), KRC_ARGS (extra compiler flags)

if(NOT DEFINED KRC OR NOT DEFINED SOURCE OR NOT DEFINED OUT1 OR NOT DEFINED OUT2)
    message(FATAL_ERROR "KRC, SOURCE, OUT1, and OUT2 are required")
endif()

set(target_args)
if(DEFINED KRC_TARGET AND NOT KRC_TARGET STREQUAL "")
    list(APPEND target_args "--target=${KRC_TARGET}")
endif()
set(extra_args)
if(DEFINED KRC_ARGS AND NOT KRC_ARGS STREQUAL "")
    list(APPEND extra_args ${KRC_ARGS})
endif()

foreach(output IN ITEMS "${OUT1}" "${OUT2}")
    execute_process(
        COMMAND "${KRC}" -c ${target_args} ${extra_args} "${SOURCE}" -o "${output}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "compilation failed for ${output}: ${stderr}${stdout}")
    endif()
endforeach()

file(SHA256 "${OUT1}" first_hash)
file(SHA256 "${OUT2}" second_hash)
file(REMOVE "${OUT1}" "${OUT2}")

if(NOT first_hash STREQUAL second_hash)
    message(FATAL_ERROR
        "output is not deterministic: ${first_hash} != ${second_hash}")
endif()

