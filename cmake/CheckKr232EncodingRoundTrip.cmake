# Assembles address spellings, disassembles the image with the same disassembler
# a reader uses, and requires each spelling to come back as the form the table
# names.
#
# A spelling with a zero displacement canonicalises: `[sp+0]` assembles to the
# compact form and disassembles as `[sp]`. That is a rule, not an accident, so it
# is written in the table rather than left to a comment -- the earlier version
# simply omitted those rows and a reader could not tell whether they were tested.
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

# source|expected
set(spellings
    "ld r2, [r3]|ld r2, [r3]"
    "ld r2, [r3+0]|ld r2, [r3]"
    "ld r2, [r3+8]|ld r2, [r3+8]"
    "st [r3], r2|st [r3], r2"
    "st [r3+8], r2|st [r3+8], r2"
    "ld r2, [sp]|ld r2, [sp]"
    "ld r2, [sp+0]|ld r2, [sp]"
    "ld r2, [sp+4]|ld r2, [sp+4]"
    "ld r2, [sp-4]|ld r2, [sp-4]"
    "ld r2, [sp+8]|ld r2, [sp+8]"
    "ld r2, [sp-8]|ld r2, [sp-8]"
    "st [sp], r2|st [sp], r2"
    "st [sp+0], r2|st [sp], r2"
    "st [sp+4], r2|st [sp+4], r2"
    "st [sp-4], r2|st [sp-4], r2"
    "st [sp+8], r2|st [sp+8], r2"
    "st [sp-8], r2|st [sp-8], r2"
    "ld r2, [pc+8]|ld r2, [pc+8]")

list(LENGTH spellings spelling_count)
file(MAKE_DIRECTORY "${WORK}")

set(body "")
foreach(entry IN LISTS spellings)
    string(REPLACE "|" ";" fields "${entry}")
    list(GET fields 0 source)
    string(APPEND body "  ${source}\n")
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
                        -o "${WORK}/roundtrip.kr232"
                RESULT_VARIABLE linked ERROR_VARIABLE link_error)
if(NOT linked EQUAL 0)
    message(FATAL_ERROR "linking the spellings failed:\n${link_error}")
endif()
execute_process(COMMAND "${DIS}" "${WORK}/roundtrip.kr232"
                OUTPUT_VARIABLE disassembly ERROR_VARIABLE dis_error
                RESULT_VARIABLE disassembled)
if(NOT disassembled EQUAL 0)
    message(FATAL_ERROR "disassembling failed:\n${dis_error}")
endif()

set(failures "")
foreach(entry IN LISTS spellings)
    string(REPLACE "|" ";" fields "${entry}")
    list(GET fields 0 source)
    list(GET fields 1 expected)
    string(FIND "${disassembly}" "${expected}" at)
    if(at EQUAL -1)
        string(APPEND failures "  '${source}' should come back as '${expected}'\n")
    endif()
endforeach()
if(NOT failures STREQUAL "")
    message(FATAL_ERROR
        "these spellings do not survive assemble -> disassemble:\n${failures}"
        "disassembly was:\n${disassembly}")
endif()
message(STATUS "all ${spelling_count} spellings come back as the table says")
