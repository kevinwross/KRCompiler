# Builds the same kr232 program twice and requires byte-identical output.
#
# A deterministic compiler is what makes a recorded frame hash, a pinned image
# hash and a reproducible gate meaningful. The two builds write to different paths,
# so a path leaking into the image would show up here.
#
# Required: KRC, KRLD, CRT0, SOURCE, WORK

foreach(required KRC KRLD CRT0 SOURCE WORK)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

foreach(round 1 2)
    execute_process(COMMAND "${KRC}" -c --target kr232 "${CRT0}"
                            -o "${WORK}/det_crt0_${round}.o"
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "assembling crt0 failed: ${output}${error}")
    endif()
    execute_process(COMMAND "${KRC}" -c --target kr232 "${SOURCE}"
                            -o "${WORK}/det_program_${round}.o"
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "compiling ${SOURCE} failed: ${output}${error}")
    endif()
    execute_process(COMMAND "${KRLD}" --entry=_start
                            "${WORK}/det_crt0_${round}.o" "${WORK}/det_program_${round}.o"
                            -o "${WORK}/det_${round}.k232"
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "linking failed: ${output}${error}")
    endif()
endforeach()

file(SHA256 "${WORK}/det_1.k232" first)
file(SHA256 "${WORK}/det_2.k232" second)
if(NOT first STREQUAL second)
    message(FATAL_ERROR "two builds differ: ${first} vs ${second}")
endif()
message(STATUS "both builds hash ${first}")
