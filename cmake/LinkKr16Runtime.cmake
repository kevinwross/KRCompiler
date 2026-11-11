if(NOT DEFINED KRC OR NOT DEFINED KRLD OR NOT DEFINED SOURCE OR
   NOT DEFINED CRT0 OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "LinkKr16Runtime.cmake requires KRC, KRLD, SOURCE, CRT0, OUTPUT")
endif()

set(crt_object "${OUTPUT}.crt0.kro")
set(user_object "${OUTPUT}.user.kro")
file(REMOVE "${crt_object}" "${user_object}" "${OUTPUT}")

execute_process(COMMAND "${KRC}" -c --target=kr16 --emit-object
                        "${CRT0}" -o "${crt_object}"
                RESULT_VARIABLE crt_result)
if(NOT crt_result EQUAL 0)
    message(FATAL_ERROR "krc failed to assemble the KR16 startup")
endif()

execute_process(COMMAND "${KRC}" -c --target=kr16 --emit-object
                        "${SOURCE}" -o "${user_object}"
                RESULT_VARIABLE user_result)
if(NOT user_result EQUAL 0)
    message(FATAL_ERROR "krc failed to compile the KR16 source")
endif()

execute_process(COMMAND "${KRLD}" "${crt_object}" ${RUNTIME} "${user_object}"
                        -o "${OUTPUT}"
                RESULT_VARIABLE link_result)
if(NOT link_result EQUAL 0)
    message(FATAL_ERROR "krld failed to link the KR16 image")
endif()
if(NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR "krld did not create ${OUTPUT}")
endif()
file(SIZE "${OUTPUT}" output_size)
if(output_size LESS 128)
    message(FATAL_ERROR "linked KR16 image is too small")
endif()
