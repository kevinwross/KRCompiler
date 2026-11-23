# Assembles a list of address spellings, disassembles the image with the same
# disassembler a reader uses, and requires each spelling to come back verbatim.
#
# This is the test the silent encoding defects asked for: `[sp]` assembled to
# base r0 for a whole milestone, and no unit test could see it because the
# instruction was legal and the address was simply wrong.
#
# `[sp+0]` and `[rN+0]` are not listed: they canonicalise to the compact form
# without a displacement, which is why a verbatim check would fail them; `[sp]`
# is the entry that covers them.
#
# Required: KRC, KRLD, DIS, WORK

if(NOT DEFINED KRC OR NOT DEFINED KRLD OR NOT DEFINED DIS OR NOT DEFINED WORK)
    message(FATAL_ERROR "KRC, KRLD, DIS and WORK are required")
endif()
foreach(path IN ITEMS "${KRC}" "${KRLD}" "${DIS}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "not found: ${path}")
    endif()
endforeach()

set(spellings
    "ld r2, [r3]"
    "ld r2, [r3+8]"
    "st [r3], r2"
    "st [r3+8], r2"
    "ld r2, [sp]"
    "ld r2, [sp+8]"
    "ld r2, [sp-8]"
    "st [sp], r2"
    "st [sp+8], r2"
    "st [sp-8], r2"
    "ld r2, [pc+8]")

list(LENGTH spellings spelling_count)
file(MAKE_DIRECTORY "${WORK}")

set(body "")
foreach(spelling IN LISTS spellings)
    string(APPEND body "  ${spelling}\n")
endforeach()
file(WRITE "${WORK}/roundtrip.s"
     ".globl _start\n.text\n_start:\n${body}  ld r7, #0\n  sys\n")

execute_process(COMMAND "${KRC}" -c --target kr232 "${WORK}/roundtrip.s"
                        -o "${WORK}/roundtrip.o"
                RESULT_VARIABLE compiled ERROR_VARIABLE compile_error)
if(NOT compiled EQUAL 0)
    message(FATAL_ERROR "assembling the spellings failed:\n${compile_error}")
endif()
execute_process(COMMAND "${KRLD}" --entry=_start "${WORK}/roundtrip.o"
                        -o "${WORK}/roundtrip.k232"
                RESULT_VARIABLE linked ERROR_VARIABLE link_error)
if(NOT linked EQUAL 0)
    message(FATAL_ERROR "linking the spellings failed:\n${link_error}")
endif()
execute_process(COMMAND "${DIS}" "${WORK}/roundtrip.k232"
                OUTPUT_VARIABLE disassembly ERROR_VARIABLE dis_error
                RESULT_VARIABLE disassembled)
if(NOT disassembled EQUAL 0)
    message(FATAL_ERROR "disassembling failed:\n${dis_error}")
endif()

set(failures "")
foreach(spelling IN LISTS spellings)
    string(FIND "${disassembly}" "${spelling}" at)
    if(at EQUAL -1)
        string(APPEND failures "  '${spelling}' did not come back verbatim\n")
    endif()
endforeach()
if(NOT failures STREQUAL "")
    message(FATAL_ERROR
        "these spellings do not survive assemble -> disassemble:\n${failures}"
        "disassembly was:\n${disassembly}")
endif()
message(STATUS "all ${spelling_count} spellings round-trip")
