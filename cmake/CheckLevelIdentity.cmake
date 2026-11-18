# Asserts the optimisation-level contract (ADR 0049, D-049):
#   * a build with no `-O` flag is byte-identical to `-O0`, so `-O0` is the
#     default rather than a separate mode;
#   * `-O0` output is identical across two runs, so the level does not introduce
#     nondeterminism;
#   * `-O1` differs from `-O0`, so the flag is wired to a pass rather than
#     accepted and ignored.
#
# Required: KRC, SOURCE, OUT_DEFAULT, OUT_O0, OUT_O0_AGAIN, OUT_O1
# Optional: KRC_TARGET (kr32 is the default)

if(NOT DEFINED KRC OR NOT DEFINED SOURCE OR NOT DEFINED OUT_DEFAULT OR
   NOT DEFINED OUT_O0 OR NOT DEFINED OUT_O0_AGAIN OR NOT DEFINED OUT_O1)
    message(FATAL_ERROR
        "KRC, SOURCE, OUT_DEFAULT, OUT_O0, OUT_O0_AGAIN and OUT_O1 are required")
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
compile_level("${OUT_O0_AGAIN}" -O0)
compile_level("${OUT_O1}" -O1)

file(SHA256 "${OUT_DEFAULT}" default_hash)
file(SHA256 "${OUT_O0}" o0_hash)
file(SHA256 "${OUT_O0_AGAIN}" o0_again_hash)
file(SHA256 "${OUT_O1}" o1_hash)
file(REMOVE "${OUT_DEFAULT}" "${OUT_O0}" "${OUT_O0_AGAIN}" "${OUT_O1}")

if(NOT default_hash STREQUAL o0_hash)
    message(FATAL_ERROR
        "no flag and -O0 differ: ${default_hash} != ${o0_hash}")
endif()

if(NOT o0_hash STREQUAL o0_again_hash)
    message(FATAL_ERROR
        "-O0 is not deterministic: ${o0_hash} != ${o0_again_hash}")
endif()

if(o1_hash STREQUAL o0_hash)
    message(FATAL_ERROR
        "-O1 produced the same output as -O0; the flag is not wired to a pass")
endif()
