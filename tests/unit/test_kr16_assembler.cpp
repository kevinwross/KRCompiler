#include "asm/kr16_assembler.h"

#include <cstdint>
#include <string>

#include "doctest.h"

using namespace krc::asmkr16;

TEST_CASE("kr16 assembler sizes extended MOV immediates") {
    const std::string source = R"(
.text
_start:
    MOV R1, #104
    MOV R0, #1
    HLT
)";
    AssemblyResult result;
    std::string error;
    REQUIRE_MESSAGE(assemble(source, result, error), error);
    REQUIRE(result.text.size() == 8);
    CHECK(result.text[0] == 0x20);
    CHECK(result.text[1] == 0xF0);
    CHECK(result.text[2] == 0x68);
    CHECK(result.text[3] == 0x00);
}


TEST_CASE("kr16 assembler emits instructions and resolves local labels") {
    const std::string source = R"(
.text
.global _start
_start:
    MOV R0, #7
    MOV R1, #0
loop:
    ADD R1, #1
    CMP R1, R0
    BNE loop
    HLT
)";
    AssemblyResult result;
    std::string error;
    REQUIRE_MESSAGE(assemble(source, result, error), error);
    REQUIRE(result.text.size() == 12);
    CHECK(result.text[0] == 0x87);
    CHECK(result.text[1] == 0x11);
    CHECK(result.text[2] == 0x80);
    CHECK(result.text[3] == 0x13);
    CHECK(result.text[4] == 0x01);
    CHECK(result.text[5] == 0x12);
    CHECK(result.text[6] == 0x04);
    CHECK(result.text[7] == 0x02);
    CHECK(result.text[8] == 0xFD);
    CHECK(result.text[9] == 0x71);
    CHECK(result.text[10] == 0x00);
    CHECK(result.text[11] == 0x88);
    REQUIRE(result.symbols.size() == 2);
    CHECK(result.symbols[0].name == "_start");
    CHECK(result.symbols[0].global);
    CHECK(result.symbols[0].offset == 0);
    CHECK(result.symbols[1].name == "loop");
    CHECK(result.symbols[1].offset == 4);
}

TEST_CASE("kr16 assembler rejects malformed input") {
    AssemblyResult result;
    std::string error;
    CHECK_FALSE(assemble(".text\nUNKNOWN R0\n", result, error));
    CHECK_FALSE(assemble(".text\nBEQ missing\n", result, error));
}
