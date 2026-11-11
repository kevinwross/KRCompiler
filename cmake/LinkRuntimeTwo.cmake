if(NOT DEFINED KRC OR NOT DEFINED KRLD OR NOT DEFINED SOURCE1 OR
   NOT DEFINED SOURCE2 OR NOT DEFINED LIBC OR NOT DEFINED IO OR
   NOT DEFINED RUNTIME OR
   NOT DEFINED OUTPUT)
    message(FATAL_ERROR
            "LinkRuntimeTwo.cmake requires KRC, KRLD, SOURCE1, SOURCE2, LIBC, IO, RUNTIME, OUTPUT")
endif()

set(object1 "${OUTPUT}.one.kro")
set(object2 "${OUTPUT}.two.kro")
foreach(path "${object1}" "${object2}" "${OUTPUT}")
    file(REMOVE "${path}")
endforeach()

execute_process(COMMAND "${KRC}" -c "${SOURCE1}" -o "${object1}"
                RESULT_VARIABLE first_result)
if(NOT first_result EQUAL 0)
    message(FATAL_ERROR "krc failed for ${SOURCE1}")
endif()

execute_process(COMMAND "${KRC}" -c "${SOURCE2}" -o "${object2}"
                RESULT_VARIABLE second_result)
if(NOT second_result EQUAL 0)
    message(FATAL_ERROR "krc failed for ${SOURCE2}")
endif()

execute_process(COMMAND "${KRLD}" "${object1}" "${object2}" "${LIBC}" "${IO}" "${RUNTIME}"
                        -o "${OUTPUT}"
                RESULT_VARIABLE link_result)
if(NOT link_result EQUAL 0)
    message(FATAL_ERROR "krld failed to link the two-object image")
endif()
if(NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR "krld did not create ${OUTPUT}")
endif()
