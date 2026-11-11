if(NOT DEFINED KRC OR NOT DEFINED KRLD OR NOT DEFINED CRT0 OR
   NOT DEFINED SOURCE1 OR NOT DEFINED SOURCE2 OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR
            "LinkKr16Multi.cmake requires KRC, KRLD, CRT0, SOURCE1, SOURCE2, OUTPUT")
endif()

set(crt_object "${OUTPUT}.crt0.kro")
set(object1 "${OUTPUT}.one.kro")
set(object2 "${OUTPUT}.two.kro")
file(REMOVE "${crt_object}" "${object1}" "${object2}" "${OUTPUT}")

execute_process(COMMAND "${KRC}" -c --target=kr16 --emit-object
                        "${CRT0}" -o "${crt_object}"
                RESULT_VARIABLE crt_result)
if(NOT crt_result EQUAL 0)
    message(FATAL_ERROR "krc failed to assemble ${CRT0}")
endif()

execute_process(COMMAND "${KRC}" -c --target=kr16 --emit-object
                        "${SOURCE1}" -o "${object1}"
                RESULT_VARIABLE one_result)
if(NOT one_result EQUAL 0)
    message(FATAL_ERROR "krc failed to compile ${SOURCE1}")
endif()

execute_process(COMMAND "${KRC}" -c --target=kr16 --emit-object
                        "${SOURCE2}" -o "${object2}"
                RESULT_VARIABLE two_result)
if(NOT two_result EQUAL 0)
    message(FATAL_ERROR "krc failed to compile ${SOURCE2}")
endif()

execute_process(COMMAND "${KRLD}" "${crt_object}" "${object1}" "${object2}"
                        -o "${OUTPUT}"
                RESULT_VARIABLE link_result)
if(NOT link_result EQUAL 0)
    message(FATAL_ERROR "krld failed to link the multi-object KR16 image")
endif()
if(NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR "krld did not create ${OUTPUT}")
endif()
