if(NOT DEFINED KRC OR NOT DEFINED KRLD OR NOT DEFINED CRT0 OR
   NOT DEFINED SOURCE OR NOT DEFINED OUT1 OR NOT DEFINED OUT2)
    message(FATAL_ERROR
            "CheckLinkDeterminism.cmake requires KRC, KRLD, CRT0, SOURCE, OUT1, OUT2")
endif()

set(crt_object "${OUT1}.crt0.kro")
set(user_object "${OUT1}.user.kro")
foreach(path "${crt_object}" "${user_object}" "${OUT1}" "${OUT2}")
    file(REMOVE "${path}")
endforeach()

execute_process(COMMAND "${KRC}" -c --target=kr16 --emit-object
                        "${CRT0}" -o "${crt_object}"
                RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "krc failed to assemble the KR16 startup")
endif()

execute_process(COMMAND "${KRC}" -c --target=kr16 --emit-object
                        "${SOURCE}" -o "${user_object}"
                RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "krc failed to compile the KR16 source")
endif()

foreach(output IN ITEMS "${OUT1}" "${OUT2}")
    execute_process(COMMAND "${KRLD}" "${crt_object}" "${user_object}"
                            -o "${output}"
                    RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "krld failed for ${output}")
    endif()
endforeach()

file(SHA256 "${OUT1}" first_hash)
file(SHA256 "${OUT2}" second_hash)
file(REMOVE "${OUT1}" "${OUT2}" "${crt_object}" "${user_object}")

if(NOT first_hash STREQUAL second_hash)
    message(FATAL_ERROR
        "linked image is not deterministic: ${first_hash} != ${second_hash}")
endif()
