# Links a KR16 image from the startup object, the host-service wrappers, the
# file layer, and one C source.
#
# The wrappers are assembled and the file layer is compiled separately from the C
# runtime library, so a test states that it needs both. The staged objects come
# from the build's sysroot, which the runtime target produces.
#
# Required: KRC, KRLD, CRT0, HOST, FILEIO, SOURCE, OUTPUT

if(NOT DEFINED KRC OR NOT DEFINED KRLD OR NOT DEFINED CRT0 OR
   NOT DEFINED HOST OR NOT DEFINED FILEIO OR NOT DEFINED SOURCE OR
   NOT DEFINED OUTPUT)
    message(FATAL_ERROR
            "LinkKr16Host.cmake requires KRC, KRLD, CRT0, HOST, FILEIO, SOURCE, OUTPUT")
endif()

set(crt_object "${OUTPUT}.crt0.kro")
set(user_object "${OUTPUT}.user.kro")
file(REMOVE "${crt_object}" "${user_object}" "${OUTPUT}")

foreach(pair IN ITEMS "${CRT0}|${crt_object}" "${SOURCE}|${user_object}")
    string(FIND "${pair}" "|" separator)
    string(SUBSTRING "${pair}" 0 ${separator} input)
    math(EXPR after "${separator} + 1")
    string(SUBSTRING "${pair}" ${after} -1 object)
    execute_process(COMMAND "${KRC}" -c --target=kr16 --emit-object
                            "${input}" -o "${object}"
                    RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "krc failed to assemble or compile ${input}")
    endif()
endforeach()

execute_process(COMMAND "${KRLD}" "${crt_object}" "${HOST}" "${FILEIO}"
                        "${user_object}" -o "${OUTPUT}"
                RESULT_VARIABLE link_result)
if(NOT link_result EQUAL 0)
    message(FATAL_ERROR "krld failed to link the KR16 host-service image")
endif()
if(NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR "krld did not create ${OUTPUT}")
endif()
