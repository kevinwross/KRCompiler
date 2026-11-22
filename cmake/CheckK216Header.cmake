# Checks the fixed header fields of a K216 image produced by a CLI test.
#
# Required: IMAGE, ENTRY, ENTRY_HEX, SECTION_COUNT
# ENTRY is the human-readable value; ENTRY_HEX is its two bytes in file order.
# The offsets are ADR 0054's frozen 64-byte header.

if(NOT DEFINED IMAGE OR NOT DEFINED ENTRY OR NOT DEFINED ENTRY_HEX OR
   NOT DEFINED SECTION_COUNT)
    message(FATAL_ERROR "IMAGE, ENTRY, ENTRY_HEX and SECTION_COUNT are required")
endif()
if(NOT EXISTS "${IMAGE}")
    message(FATAL_ERROR "image not found: ${IMAGE}")
endif()

file(READ "${IMAGE}" magic HEX OFFSET 0 LIMIT 4)
file(READ "${IMAGE}" version_hex OFFSET 4 LIMIT 2 HEX)
file(READ "${IMAGE}" kind_hex OFFSET 6 LIMIT 2 HEX)
file(READ "${IMAGE}" entry_hex OFFSET 16 LIMIT 2 HEX)
file(READ "${IMAGE}" abi_hex OFFSET 18 LIMIT 2 HEX)
file(READ "${IMAGE}" sections_hex OFFSET 20 LIMIT 2 HEX)
file(READ "${IMAGE}" target_hex OFFSET 22 LIMIT 2 HEX)

if(NOT magic STREQUAL "4b323136")
    message(FATAL_ERROR "expected K216 magic, got ${magic}")
endif()
if(NOT version_hex STREQUAL "0100")
    message(FATAL_ERROR "expected container version 1, got ${version_hex}")
endif()
if(NOT kind_hex STREQUAL "0200")
    message(FATAL_ERROR "expected executable kind 2, got ${kind_hex}")
endif()
string(TOLOWER "${ENTRY_HEX}" expected_entry_hex)
if(NOT entry_hex STREQUAL expected_entry_hex)
    message(FATAL_ERROR "expected entry ${ENTRY} (${expected_entry_hex}), got ${entry_hex}")
endif()
if(NOT abi_hex STREQUAL "0100")
    message(FATAL_ERROR "expected ABI version 1, got ${abi_hex}")
endif()
if(NOT SECTION_COUNT EQUAL 2 OR NOT sections_hex STREQUAL "0200")
    message(FATAL_ERROR "expected two sections, got ${SECTION_COUNT}/${sections_hex}")
endif()
if(NOT target_hex STREQUAL "0400")
    message(FATAL_ERROR "expected target ID 4, got ${target_hex}")
endif()
