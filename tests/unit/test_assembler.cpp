#include <cstdint>
#include <ostream>
#include <string>

#include "asm/kr32_assembler.h"
#include "doctest.h"

using krc::asmkr32::AssemblyResult;

namespace {

AssemblyResult assemble_or_fail(const std::string& source) {
    AssemblyResult result;
    std::string error;
    REQUIRE_MESSAGE(krc::asmkr32::assemble(source, result, error), error);
    return result;
}

}  // namespace

TEST_CASE("KR32 assembler encodes simple instructions and labels") {
    const auto result = assemble_or_fail(R"ASM(
        .text
        .global _start
_start:
        MOV R1, #42
        ADD R3, R1, R2
        RET
)ASM");
    REQUIRE(result.text.size() == 12);
    CHECK(result.text[0] == 0x2A);
    CHECK(result.text[1] == 0x08);
    CHECK(result.text[2] == 0x20);
    CHECK(result.text[3] == 0x00);
    CHECK(result.text[4] == 0x00);
    CHECK(result.text[5] == 0x40);
    CHECK(result.text[6] == 0x62);
    CHECK(result.text[7] == 0x40);
    CHECK(result.text[8] == 0x00);
    CHECK(result.text[9] == 0x00);
    CHECK(result.text[10] == 0x00);
    CHECK(result.text[11] == 0x76);
    REQUIRE(result.symbols.size() == 1);
    CHECK(result.symbols[0].name == "_start");
    CHECK(result.symbols[0].global);
}

TEST_CASE("KR32 assembler lays out data and records external relocations") {
    const auto result = assemble_or_fail(R"ASM(
        .data
msg:
        .byte 72, 105, 0
        .text
        .global _start
        .extern putchar
_start:
        LEA R0, [msg]
        MOV R1, #0
        CALL putchar
        JZ done
done:
        HALT
)ASM");
    REQUIRE(result.data.size() == 3);
    CHECK(result.data[0] == 72);
    CHECK(result.data[1] == 105);
    CHECK(result.data[2] == 0);
    CHECK(result.text.size() == 32);
    REQUIRE(result.symbols.size() == 3);
    CHECK(result.symbols[0].name == "_start");
    CHECK(result.symbols[1].name == "done");
    CHECK(result.symbols[2].name == "msg");
    CHECK(result.symbols[2].address == 32);
    REQUIRE(result.relocations.size() == 3);
    CHECK(result.relocations[0].symbol == "msg");
    CHECK(result.relocations[0].offset == 4);
    CHECK(result.relocations[1].symbol == "putchar");
    CHECK(result.relocations[1].offset == 16);
    CHECK(result.relocations[2].symbol == "done");
}

TEST_CASE("KR32 assembler rejects undefined symbols") {
    AssemblyResult result;
    std::string error;
    CHECK_FALSE(krc::asmkr32::assemble(R"ASM(
        .text
        CALL missing
        RET
)ASM",
                                       result, error));
    CHECK(error.find("undefined symbol") != std::string::npos);
}
