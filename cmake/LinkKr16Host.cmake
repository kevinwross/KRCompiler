# Links a KR16 image from the startup object, the host-service wrappers, the
# file layer, and one or two C sources.
#
# The wrappers are assembled and the file layer is compiled separately from the C
# runtime library, so a test states that it needs both. The staged objects come
# from the build's sysroot, which the runtime target produces.
#
# A second source states that two translation units link together: a template
# instantiated in both is emitted weak in each, and the linker keeps one.
#
# Required: KRC, KRLD, CRT0, HOST, FILEIO, SOURCE, OUTPUT
# Optional: SOURCE2

if(NOT DEFINED KRC OR NOT DEFINED KRLD OR NOT DEFINED CRT0 OR
   NOT DEFINED HOST OR NOT DEFINED FILEIO OR NOT DEFINED SOURCE OR
   NOT DEFINED OUTPUT)
    message(FATAL_ERROR
            "LinkKr16Host.cmake requires KRC, KRLD, CRT0, HOST, FILEIO, SOURCE, OUTPUT")
endif()

set(crt_object "${OUTPUT}.crt0.kro")
set(user_object "${OUTPUT}.user.kro")
set(user_objects "${user_object}")
set(inputs "${CRT0}|${crt_object}" "${SOURCE}|${user_object}")

# The C library is optional: a fixture that calls printf needs it, and one that
# does not would only grow the link. LIBCRT names the staged libcrt object.
set(libraries "${HOST}" "${FILEIO}")
if(DEFINED LIBCRT AND NOT LIBCRT STREQUAL "")
    list(APPEND libraries "${LIBCRT}")
endif()

if(DEFINED SOURCE2 AND NOT SOURCE2 STREQUAL "")
    set(user_object2 "${OUTPUT}.user2.kro")
    list(APPEND inputs "${SOURCE2}|${user_object2}")
    list(APPEND user_objects "${user_object2}")
endif()

file(REMOVE "${crt_object}" ${user_objects} "${OUTPUT}")

# Optional: LEVEL is a compiler flag such as `-O1`, applied to every compile this
# script performs, so an image can be linked at a named optimisation level.
set(level_args)
if(DEFINED LEVEL AND NOT LEVEL STREQUAL "")
    list(APPEND level_args "${LEVEL}")
endif()

foreach(pair IN LISTS inputs)
    string(FIND "${pair}" "|" separator)
    if(separator EQUAL -1)
        # Without the separator the slices below take the whole string twice and
        # hand krc a nonsense object name; name the malformed pair instead.
        message(FATAL_ERROR "an input pair is malformed: '${pair}'")
    endif()
    string(SUBSTRING "${pair}" 0 ${separator} input)
    math(EXPR after "${separator} + 1")
    string(SUBSTRING "${pair}" ${after} -1 object)
    execute_process(COMMAND "${KRC}" -c --target=kr16 ${level_args} --emit-object
                            "${input}" -o "${object}"
                    RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "krc failed to assemble or compile ${input}")
    endif()
endforeach()

set(gc_flags)
if(DEFINED GC_SECTIONS AND NOT GC_SECTIONS STREQUAL "")
    # Object-level collection: drop objects the entry point cannot reach.
    list(APPEND gc_flags --gc-sections)
endif()

execute_process(COMMAND "${KRLD}" "${crt_object}" ${libraries} ${gc_flags}
                        ${user_objects} -o "${OUTPUT}"
                RESULT_VARIABLE link_result)
if(NOT link_result EQUAL 0)
    message(FATAL_ERROR "krld failed to link the KR16 host-service image")
endif()
if(NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR "krld did not create ${OUTPUT}")
endif()
