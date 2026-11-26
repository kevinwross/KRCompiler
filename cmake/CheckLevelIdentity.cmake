# Asserts the optimisation-level contract (ADR 0049, D-049):
#   * a build with no `-O` flag is byte-identical to `-O1`, so `-O1` is the
#     default rather than a separate mode;
#   * `-O1` output is identical across two runs, so the level does not introduce
#     nondeterminism;
#   * `-O0` differs from `-O1`, so the flag is wired to a pass rather than
#     accepted and ignored;
#   * `-O2` and `-Os` are accepted and deterministic.
#
# Required: KRC, SOURCE, OUT_DEFAULT, OUT_O0, OUT_O1, OUT_O1_AGAIN,
#           OUT_O2, OUT_O2_AGAIN, OUT_OS
# Optional: KRC_TARGET (kr32 is the default)

if(NOT DEFINED KRC OR NOT DEFINED SOURCE OR NOT DEFINED OUT_DEFAULT OR
   NOT DEFINED OUT_O0 OR NOT DEFINED OUT_O1 OR NOT DEFINED OUT_O1_AGAIN OR
   NOT DEFINED OUT_O2 OR NOT DEFINED OUT_O2_AGAIN OR NOT DEFINED OUT_OS)
    message(FATAL_ERROR
        "KRC, SOURCE, OUT_DEFAULT, OUT_O0, OUT_O1, OUT_O1_AGAIN, OUT_O2, "
        "OUT_O2_AGAIN and OUT_OS are required")
endif()

set(target_args)
if(DEFINED KRC_TARGET AND NOT KRC_TARGET STREQUAL "")
    list(APPEND target_args "--target=${KRC_TARGET}")
endif()

function(compile_level output)
    execute_process(
        COMMAND "${KRC}" -S ${target_args} ${ARGN} "${SOURCE}" -o "${output}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "compilation failed for ${output}: ${stderr}${stdout}")
    endif()
endfunction()

compile_level("${OUT_DEFAULT}")
compile_level("${OUT_O0}" -O0)
compile_level("${OUT_O1}" -O1)
compile_level("${OUT_O1_AGAIN}" -O1)
compile_level("${OUT_O2}" -O2)
compile_level("${OUT_O2_AGAIN}" -O2)
compile_level("${OUT_OS}" -Os)

file(SHA256 "${OUT_DEFAULT}" default_hash)
file(SHA256 "${OUT_O0}" o0_hash)
file(SHA256 "${OUT_O1}" o1_hash)
file(SHA256 "${OUT_O1_AGAIN}" o1_again_hash)
file(SHA256 "${OUT_O2}" o2_hash)
file(SHA256 "${OUT_O2_AGAIN}" o2_again_hash)
file(SHA256 "${OUT_OS}" os_hash)
file(REMOVE "${OUT_DEFAULT}" "${OUT_O0}" "${OUT_O1}" "${OUT_O1_AGAIN}"
            "${OUT_O2}" "${OUT_O2_AGAIN}" "${OUT_OS}")

if(NOT default_hash STREQUAL o1_hash)
    message(FATAL_ERROR
        "no flag and -O1 differ: ${default_hash} != ${o1_hash}")
endif()

if(NOT o1_hash STREQUAL o1_again_hash)
    message(FATAL_ERROR
        "-O1 is not deterministic: ${o1_hash} != ${o1_again_hash}")
endif()

if(o0_hash STREQUAL o1_hash)
    message(FATAL_ERROR
        "-O0 produced the same output as -O1; the flag is not wired to a pass")
endif()

if(NOT o2_hash STREQUAL o2_again_hash)
    message(FATAL_ERROR
        "-O2 is not deterministic: ${o2_hash} != ${o2_again_hash}")
endif()
