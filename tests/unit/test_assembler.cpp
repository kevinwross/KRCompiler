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

TEST_CASE("KR32 assembler encodes the tile and transform instructions") {
    const auto result = assemble_or_fail(R"ASM(
        .text
        MLOAD T1, [R2]
        VLOAD T2, [R3]
        MMUL T0, T1, T2
        MMAC T0, T1, T2
        MTRANS T3, T0
        MVEC T4, T0, T2
        VDOT F0, T4, T2
        MSTORE T4, [R5]
        VSTORE T4, [R5]
        MPUSH T4
        MPOP T4
        MDROP
        MLOAD T1, [R2], #64
        RET
)ASM");
    REQUIRE(result.text.size() == 60);
    const auto word_at = [&](std::size_t index) {
        return static_cast<std::uint32_t>(result.text[index * 4]) |
               (static_cast<std::uint32_t>(result.text[index * 4 + 1]) << 8) |
               (static_cast<std::uint32_t>(result.text[index * 4 + 2]) << 16) |
               (static_cast<std::uint32_t>(result.text[index * 4 + 3]) << 24);
    };
    CHECK(word_at(0) == 0xA4244000u);   // MLOAD T1, [R2]
    CHECK(word_at(1) == 0xA4464800u);   // VLOAD T2, [R3]
    CHECK(word_at(2) == 0xC2024000u);   // MMUL T0, T1, T2
    CHECK(word_at(3) == 0xC2024001u);   // MMAC T0, T1, T2
    CHECK(word_at(4) == 0xC2600003u);   // MTRANS T3, T0
    CHECK(word_at(5) == 0xC2804002u);   // MVEC T4, T0, T2
    CHECK(word_at(6) == 0xC2084004u);   // VDOT F0, T4, T2
    CHECK(word_at(7) == 0xA68A4000u);   // MSTORE T4, [R5]
    CHECK(word_at(8) == 0xA68A4800u);   // VSTORE T4, [R5]
    CHECK(word_at(9) == 0xC2800005u);   // MPUSH T4
    CHECK(word_at(10) == 0xC2800006u);  // MPOP T4
    CHECK(word_at(11) == 0xC2000007u);  // MDROP
    CHECK(word_at(12) == 0xA4245000u);  // MLOAD T1, [R2], #64
    CHECK(word_at(13) == 64u);          // row stride
    CHECK(word_at(14) == 0x76000000u);  // RET
}

TEST_CASE("KR32 assembler rejects a misaligned tile operand") {
    AssemblyResult result;
    std::string error;
    CHECK_FALSE(krc::asmkr32::assemble(R"ASM(
        .text
        MMUL R0, T1, T2
        RET
)ASM",
                                       result, error));
    CHECK(error.find("tile register") != std::string::npos);
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
TEST_CASE("KR32 assembler relocates symbolic data words") {
    const auto result = assemble_or_fail(R"ASM(
        .data
table:
        .word handler, 0, table
        .text
        .global handler
handler:
        RET
)ASM");
    REQUIRE(result.data.size() == 12);
    REQUIRE(result.relocations.size() == 2);
    CHECK(result.relocations[0].symbol == "handler");
    CHECK(result.relocations[0].section == 1);
    CHECK(result.relocations[0].offset == 0);
    CHECK(result.relocations[1].symbol == "table");
    CHECK(result.relocations[1].section == 1);
    CHECK(result.relocations[1].offset == 8);
    CHECK(result.data[4] == 0);
    CHECK(result.data[5] == 0);
}
