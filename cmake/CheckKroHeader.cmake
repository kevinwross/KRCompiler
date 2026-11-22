# Checks the fixed header fields of a KRO object produced by a CLI test.
#
# Required: OBJECT, TARGET_ID, POINTER_WIDTH, SECTION_COUNT
# The offsets are docs/object.md's frozen v1 header. This script deliberately
# reads only the fields a command-line test needs to prove; the unit tests read
# the whole object through the KRO reader.

if(NOT DEFINED OBJECT OR NOT DEFINED TARGET_ID OR NOT DEFINED POINTER_WIDTH OR
   NOT DEFINED SECTION_COUNT)
    message(FATAL_ERROR "OBJECT, TARGET_ID, POINTER_WIDTH and SECTION_COUNT are required")
endif()
if(NOT EXISTS "${OBJECT}")
    message(FATAL_ERROR "object not found: ${OBJECT}")
endif()

file(READ "${OBJECT}" kind_hex OFFSET 6 LIMIT 2 HEX)
file(READ "${OBJECT}" target_hex OFFSET 12 LIMIT 4 HEX)
file(READ "${OBJECT}" pointer_hex OFFSET 20 LIMIT 1 HEX)
file(READ "${OBJECT}" sections_hex OFFSET 24 LIMIT 4 HEX)

if(NOT kind_hex STREQUAL "0000")
    message(FATAL_ERROR "expected object kind 0, got ${kind_hex}")
endif()
if(NOT TARGET_ID EQUAL 4 OR NOT target_hex STREQUAL "04000000")
    message(FATAL_ERROR "expected target 4, got ${TARGET_ID}/${target_hex}")
endif()
if(NOT POINTER_WIDTH EQUAL 16 OR NOT pointer_hex STREQUAL "10")
    message(FATAL_ERROR "expected a 16-bit pointer, got ${POINTER_WIDTH}/${pointer_hex}")
endif()
if(NOT SECTION_COUNT EQUAL 1 OR NOT sections_hex STREQUAL "01000000")
    message(FATAL_ERROR "expected one section, got ${SECTION_COUNT}/${sections_hex}")
endif()
