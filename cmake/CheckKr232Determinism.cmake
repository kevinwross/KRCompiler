# Builds the same kr232 program twice and requires byte-identical output.
#
# A deterministic compiler is what makes a recorded frame hash, a pinned image
# hash and a reproducible gate meaningful. The two builds write to different paths,
# so a path leaking into the image would show up here.
#
# The work directory belongs to the row that owns it: create it rather than
# assuming the shared test output directory already exists.
file(MAKE_DIRECTORY "${WORK}")
 
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
                            -o "${WORK}/det_${round}.kr232"
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "linking failed: ${output}${error}")
    endif()
endforeach()

file(SHA256 "${WORK}/det_1.kr232" first)
file(SHA256 "${WORK}/det_2.kr232" second)
if(NOT first STREQUAL second)
    message(FATAL_ERROR "two builds differ: ${first} vs ${second}")
endif()

message(STATUS "both builds hash ${first}")

# The assembly text must be identical too: byte-identical images are meaningless
# if the text that produced them is not reproducible.
# -S writes a file named for the target, so the two runs name their outputs
# explicitly and the comparison reads the files rather than stdout.
execute_process(COMMAND "${KRC}" -S --target kr232 "${SOURCE}" -o "${WORK}/first.s"
                OUTPUT_VARIABLE first_ignored ERROR_VARIABLE assembly_error
                RESULT_VARIABLE assembly_result)
if(NOT assembly_result EQUAL 0)
    message(FATAL_ERROR "emitting assembly failed: ${assembly_error}")
endif()
execute_process(COMMAND "${KRC}" -S --target kr232 "${SOURCE}" -o "${WORK}/second.s"
                OUTPUT_VARIABLE second_ignored ERROR_VARIABLE assembly_error
                RESULT_VARIABLE assembly_result)
if(NOT assembly_result EQUAL 0)
    message(FATAL_ERROR "emitting assembly failed: ${assembly_error}")
endif()
file(READ "${WORK}/first.s" first_assembly)
file(READ "${WORK}/second.s" second_assembly)
if(NOT first_assembly STREQUAL second_assembly)
    message(FATAL_ERROR "two -S runs differ")
endif()
message(STATUS "assembly text is identical across runs")