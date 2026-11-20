if(NOT DEFINED KRC OR NOT DEFINED KRLD OR NOT DEFINED SOURCE OR
   NOT DEFINED OUTPUT)
    message(FATAL_ERROR "AssembleKr16.cmake requires KRC, KRLD, SOURCE and OUTPUT")
endif()

set(object "${OUTPUT}.kro")
file(REMOVE "${object}" "${OUTPUT}")
execute_process(COMMAND "${KRC}" -c --target=kr16 --emit-object
                        "${SOURCE}" -o "${object}"
                RESULT_VARIABLE assemble_result)
if(NOT assemble_result EQUAL 0)
    message(FATAL_ERROR "krc failed to assemble ${SOURCE} (${assemble_result})")
endif()
execute_process(COMMAND "${KRLD}" "${object}" --entry=_start -o "${OUTPUT}"
                RESULT_VARIABLE link_result)
if(NOT link_result EQUAL 0)
    message(FATAL_ERROR "krld failed to link ${OUTPUT} (${link_result})")
endif()
if(NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR "krld did not create ${OUTPUT}")
endif()
