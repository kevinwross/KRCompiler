# Builds kr16's shipped file I/O test against krc.
#
# The runtime sources are read from the sibling kr16 checkout, unmodified:
# resources/fileio.c is the wrapper layer that calls `__host_call`, libc.s is
# the shipped C library, and tests/kevin/fileio.c is the program kr16's own
# `kr16.fileio.run` test executes. The startup is a test fixture because the
# shipped libc already defines `puts` and `abort`, which krc's runtime startup
# also defines for its own libc.
#
# Required: KRC, KRLD, KR16_SOURCE, CRT0, OUTPUT
# Optional: SYSROOT
if(NOT DEFINED KRC OR NOT DEFINED KRLD OR NOT DEFINED KR16_SOURCE OR
   NOT DEFINED CRT0 OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR
        "LinkKr16ShippedFileio.cmake requires KRC, KRLD, KR16_SOURCE, CRT0, OUTPUT")
endif()

set(crt_object "${OUTPUT}.crt0.kro")
set(fileio_object "${OUTPUT}.fileio.kro")
set(test_object "${OUTPUT}.test.kro")
set(libc_object "${OUTPUT}.libc.kro")
file(REMOVE "${crt_object}" "${fileio_object}" "${test_object}" "${libc_object}"
     "${OUTPUT}")

set(sysroot_args)
if(DEFINED SYSROOT AND NOT SYSROOT STREQUAL "")
    list(APPEND sysroot_args --sysroot "${SYSROOT}")
endif()

foreach(step IN ITEMS crt0 fileio test libc)
    if(step STREQUAL "crt0")
        set(source "${CRT0}")
        set(object "${crt_object}")
        set(args)
    elseif(step STREQUAL "fileio")
        set(source "${KR16_SOURCE}/resources/fileio.c")
        set(object "${fileio_object}")
        set(args -I "${KR16_SOURCE}/include")
    elseif(step STREQUAL "test")
        set(source "${KR16_SOURCE}/tests/kevin/fileio.c")
        set(object "${test_object}")
        set(args -I "${KR16_SOURCE}/include")
    else()
        set(source "${KR16_SOURCE}/resources/libc.s")
        set(object "${libc_object}")
        set(args)
    endif()
    execute_process(
        COMMAND "${KRC}" -c --target=kr16 --emit-object ${args} ${sysroot_args}
                "${source}" -o "${object}"
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "krc failed to build the shipped ${step} (${source})")
    endif()
endforeach()

execute_process(
    COMMAND "${KRLD}" "${crt_object}" "${fileio_object}" "${test_object}"
            "${libc_object}" -o "${OUTPUT}"
    RESULT_VARIABLE link_result)
if(NOT link_result EQUAL 0)
    message(FATAL_ERROR "krld failed to link the shipped fileio image")
endif()
if(NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR "krld did not create ${OUTPUT}")
endif()
