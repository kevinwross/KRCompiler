if(NOT DEFINED KRC OR NOT DEFINED KRLD OR NOT DEFINED SOURCE OR
   NOT DEFINED RUNTIME OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "LinkRuntime.cmake requires KRC, KRLD, SOURCE, RUNTIME, OUTPUT")
endif()

set(object "${OUTPUT}.kro")
# Optional: LEVEL is a compiler flag such as `-O1`, applied to the program and,
# unless LIBC_LEVEL overrides it, to the runtime source. LIBC_LEVEL lets the
# program and the runtime be built at different levels.
set(level_args)
if(DEFINED LEVEL AND NOT LEVEL STREQUAL "")
    list(APPEND level_args "${LEVEL}")
endif()
# Optional: LIBC_LEVEL overrides LEVEL for the runtime source, so a program can
# be built at one level against a runtime built at another. When LIBC names a
# prebuilt .kro no runtime compile happens and this has no effect.
set(libc_level_args ${level_args})
if(DEFINED LIBC_LEVEL AND NOT LIBC_LEVEL STREQUAL "")
    set(libc_level_args "${LIBC_LEVEL}")
endif()
file(REMOVE "${object}" "${OUTPUT}")
execute_process(COMMAND "${KRC}" -c ${level_args} "${SOURCE}" -o "${object}"
                RESULT_VARIABLE compile_result)
if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR "krc -c failed with ${compile_result}")
endif()
set(extra_objects)
if(DEFINED LIBC AND NOT LIBC STREQUAL "")
    if(LIBC MATCHES "\\.kro$")
        set(libc_object "${LIBC}")
    else()
        set(libc_object "${OUTPUT}.libc.kro")
        file(REMOVE "${libc_object}")
        execute_process(COMMAND "${KRC}" -c ${libc_level_args} "${LIBC}" -o "${libc_object}"
                        RESULT_VARIABLE libc_result)
        if(NOT libc_result EQUAL 0)
            message(FATAL_ERROR "krc -c runtime libc failed with ${libc_result}")
        endif()
    endif()
    list(APPEND extra_objects "${libc_object}")
    if(NOT DEFINED IO OR IO STREQUAL "")
        get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
        set(IO "${root}/targets/kr32/runtime/fileio.s")
    endif()
endif()
if(DEFINED IO AND NOT IO STREQUAL "")
    list(APPEND extra_objects "${IO}")
endif()
# The exception runtime is self-contained assembly (ADR 0026): linking it into
# an image that never throws costs a few hundred bytes and keeps the link
# recipe uniform.
get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(EXISTS "${root}/targets/kr32/runtime/eh.s")
    list(APPEND extra_objects "${root}/targets/kr32/runtime/eh.s")
endif()
get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
execute_process(COMMAND "${KRLD}" "${object}" ${extra_objects} "${RUNTIME}"
                        "--source-root=${root}" -o "${OUTPUT}"
                RESULT_VARIABLE link_result)
if(NOT link_result EQUAL 0)
    message(FATAL_ERROR "krld failed with ${link_result}")
endif()
if(NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR "krld did not create ${OUTPUT}")
endif()
file(SIZE "${OUTPUT}" output_size)
if(output_size LESS 64)
    message(FATAL_ERROR "linked image is too small")
endif()
