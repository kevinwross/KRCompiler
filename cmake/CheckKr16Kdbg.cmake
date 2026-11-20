if(NOT DEFINED KRC OR NOT DEFINED SOURCE OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "CheckKr16Kdbg.cmake requires KRC, SOURCE and OUTPUT")
endif()

file(REMOVE "${OUTPUT}")
execute_process(COMMAND "${KRC}" -c --target=kr16 "${SOURCE}" -o "${OUTPUT}"
                RESULT_VARIABLE image_result)
if(NOT image_result EQUAL 0)
    message(FATAL_ERROR "krc failed to build ${OUTPUT} (${image_result})")
endif()
if(NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR "krc did not create ${OUTPUT}")
endif()

execute_process(COMMAND "${KRC}" --dump-symbols --json "${OUTPUT}"
                OUTPUT_VARIABLE json RESULT_VARIABLE json_result)
if(NOT json_result EQUAL 0)
    message(FATAL_ERROR "krc --dump-symbols --json failed (${json_result})")
endif()

execute_process(COMMAND "${KRC}" --dump-symbols "${OUTPUT}"
                OUTPUT_VARIABLE text RESULT_VARIABLE text_result)
if(NOT text_result EQUAL 0)
    message(FATAL_ERROR "krc --dump-symbols failed (${text_result})")
endif()

# The JSON form carries the target, a source path and a function name. The
# frozen schema has never carried lines or variables, so those are asserted on
# the text form.
foreach(needle IN ITEMS
        "\"target\":2"
        "kr16_sum.c"
        "\"name\":\"sum_to\"")
    string(FIND "${json}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "JSON dump is missing '${needle}'")
    endif()
endforeach()

foreach(needle IN ITEMS
        "KDBG target=2"
        "source 0 "
        "line 0x"
        "function sum_to "
        "variable sum_to.total ")
    string(FIND "${text}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "text dump is missing '${needle}'")
    endif()
endforeach()
