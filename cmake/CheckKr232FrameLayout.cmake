# Checks that every frame object ends at or below FP, and that a deep frame is
# actually exercised.
#
# The compiler records each local and parameter as
# `.debug_var <function> <name> <offset> <flags> <size>`, with the offset relative
# to FP. `[FP]` holds the saved frame pointer, so an object whose offset plus size
# is greater than zero reaches above FP and overwrites it -- a twelve-byte array at
# offset -8 is the case that produced this check.
#
# Required: KRC, KRLD, VM, CRT0, FIXTURE, WORK, EXPECT_EXIT
# Optional: EXPECT_PARAM -- a parameter that must have a record
#           MIN_DEPTH    -- the deepest offset any object must reach, so that the
#                           R8 path for displacements beyond a signed byte runs

if(NOT DEFINED KRC OR NOT DEFINED KRLD OR NOT DEFINED VM OR NOT DEFINED CRT0 OR
   NOT DEFINED FIXTURE OR NOT DEFINED WORK OR NOT DEFINED EXPECT_EXIT)
    message(FATAL_ERROR "KRC, KRLD, VM, CRT0, FIXTURE, WORK and EXPECT_EXIT are required")
endif()

file(MAKE_DIRECTORY "${WORK}")

# The assembly carries the frame record.
execute_process(COMMAND "${KRC}" -S --target kr232 "${FIXTURE}" -o "${WORK}/frame.s"
                OUTPUT_VARIABLE assembly ERROR_VARIABLE assembly_error
                RESULT_VARIABLE assembled)
if(NOT assembled EQUAL 0)
    message(FATAL_ERROR "emitting assembly failed:\n${assembly_error}")
endif()
# -S writes a file named for the target unless -o names one; the records are
# read from that file rather than from stdout.
if(NOT EXISTS "${WORK}/frame.s")
    message(FATAL_ERROR "krc -S wrote no ${WORK}/frame.s")
endif()
file(READ "${WORK}/frame.s" assembly)

string(REGEX MATCHALL ".debug_var [^\n]*" records "${assembly}")
if(records STREQUAL "")
    message(FATAL_ERROR "no .debug_var records: nothing was checked")
endif()

set(count 0)
set(widest 0)
set(deepest 0)
set(names "")
foreach(record IN LISTS records)
    separate_arguments(fields UNIX_COMMAND "${record}")
    list(LENGTH fields length)
    if(length LESS 6)
        message(FATAL_ERROR "a frame record is malformed: '${record}'")
    endif()
    list(GET fields 2 variable)
    list(GET fields 3 offset_text)
    list(GET fields 5 size_text)
    math(EXPR offset "${offset_text}")
    math(EXPR size "${size_text}")
    math(EXPR end "${offset} + ${size}")
    math(EXPR count "${count} + 1")
    string(APPEND names " ${variable} ")
    if(size GREATER widest)
        set(widest ${size})
    endif()
    if(offset LESS deepest)
        set(deepest ${offset})
    endif()
    if(end GREATER 0)
        message(FATAL_ERROR
            "frame object '${variable}' spans FP-${offset}..FP+${end}, which "
            "overwrites the saved frame pointer at [FP]")
    endif()
endforeach()

# A run with nothing wide in it would pass vacuously.
if(widest LESS 12)
    message(FATAL_ERROR
        "the widest frame object is ${widest} bytes: the fixture stopped covering "
        "a multi-word local")
endif()

if(DEFINED EXPECT_PARAM AND NOT EXPECT_PARAM STREQUAL "")
    if(NOT names MATCHES " ${EXPECT_PARAM} ")
        message(FATAL_ERROR
            "no record for the parameter '${EXPECT_PARAM}': records are${names}")
    endif()
endif()

if(DEFINED MIN_DEPTH AND NOT MIN_DEPTH STREQUAL "")
    math(EXPR magnitude "-1 * ${deepest}")
    if(magnitude LESS MIN_DEPTH)
        message(FATAL_ERROR
            "the deepest frame object is at FP-${magnitude}, short of the "
            "${MIN_DEPTH} this fixture exists to exercise")
    endif()
endif()

# And the program still has to behave.
execute_process(COMMAND "${KRC}" -c --target kr232 "${CRT0}" -o "${WORK}/crt0.o"
                RESULT_VARIABLE crt0_state ERROR_VARIABLE crt0_error)
if(NOT crt0_state EQUAL 0)
    message(FATAL_ERROR "compiling crt0 failed:\n${crt0_error}")
endif()
execute_process(COMMAND "${KRC}" -c --target kr232 "${FIXTURE}" -o "${WORK}/frame.o"
                RESULT_VARIABLE object_state ERROR_VARIABLE object_error)
if(NOT object_state EQUAL 0)
    message(FATAL_ERROR "compiling the fixture failed:\n${object_error}")
endif()
execute_process(COMMAND "${KRLD}" --entry=_start "${WORK}/crt0.o" "${WORK}/frame.o"
                        -o "${WORK}/frame.kr232"
                RESULT_VARIABLE link_state ERROR_VARIABLE link_error)
if(NOT link_state EQUAL 0)
    message(FATAL_ERROR "linking the fixture failed:\n${link_error}")
endif()
execute_process(COMMAND "${VM}" vm "${WORK}/frame.kr232"
                OUTPUT_VARIABLE output ERROR_VARIABLE error
                RESULT_VARIABLE result)
if(NOT result EQUAL EXPECT_EXIT)
    message(FATAL_ERROR "expected exit ${EXPECT_EXIT}, got ${result}\n${output}${error}")
endif()

message(STATUS
    "${count} frame object(s) end at or below FP; widest ${widest} bytes, deepest FP-${deepest}")
