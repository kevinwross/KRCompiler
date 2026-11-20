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

TEST_CASE("kr16 assembler lays out .text, .data and .bss separately") {
    const std::string source = R"(
.text
.global main
main:
    RET
.data
.byte 0x11
.align 4
.word 0x2233
.bss
.space 3
.align 4
.space 2
)";
    AssemblyResult result;
    std::string error;
    REQUIRE_MESSAGE(assemble(source, result, error), error);
    REQUIRE(result.text.size() == 2);
    REQUIRE(result.data.size() == 6);
    CHECK(result.data[0] == 0x11);
    CHECK(result.data[1] == 0x00);
    CHECK(result.data[2] == 0x00);
    CHECK(result.data[3] == 0x00);
    CHECK(result.data[4] == 0x33);
    CHECK(result.data[5] == 0x22);
    // .bss has a size but no bytes: the loader zero-fills it.
    CHECK(result.has_data);
    CHECK(result.has_bss);
    CHECK(result.bss_size == 6);
    CHECK(result.listing.size() == 7);
}

TEST_CASE("kr16 assembler keeps .bss labels at their reserved offsets") {
    const std::string source = R"(
.bss
.align 2
buffer:
  .space 8
index:
  .space 2
.text
.global main
main:
  RET
)";
    AssemblyResult result;
    std::string error;
    REQUIRE_MESSAGE(assemble(source, result, error), error);
    CHECK(result.bss_size == 10);
    REQUIRE(result.symbols.size() == 3);
    const auto find = [&](std::string_view name) {
        for (const auto& symbol : result.symbols) {
            if (symbol.name == name) {
                return symbol;
            }
        }
        return AssembledSymbol{};
    };
    CHECK(find("buffer").section == 2);
    CHECK(find("buffer").offset == 0);
    CHECK(find("index").section == 2);
    CHECK(find("index").offset == 8);
    CHECK(find("main").section == 0);
    CHECK(find("main").offset == 0);
}

TEST_CASE(".align pads with zeros and .space reserves bytes") {
    const std::string source = R"(
.text
.byte 0xAA
.align 4
.byte 0xBB
.space 3
.byte 0xCC
)";
    AssemblyResult result;
    std::string error;
    REQUIRE_MESSAGE(assemble(source, result, error), error);
    REQUIRE(result.text.size() == 9);
    CHECK(result.text[0] == 0xAA);
    CHECK(result.text[1] == 0x00);
    CHECK(result.text[2] == 0x00);
    CHECK(result.text[3] == 0x00);
    CHECK(result.text[4] == 0xBB);
    CHECK(result.text[5] == 0x00);
    CHECK(result.text[6] == 0x00);
    CHECK(result.text[7] == 0x00);
    CHECK(result.text[8] == 0xCC);
    // The align row reports the padding it produced.
    REQUIRE(result.listing.size() == 5);
    CHECK(result.listing[1].address == 1);
    CHECK(result.listing[1].bytes.size() == 3);
    CHECK(result.listing[2].address == 4);
    CHECK(result.listing[3].address == 5);
    CHECK(result.listing[3].bytes.size() == 3);
}

TEST_CASE(".word emits a little-endian 16-bit value and relocates symbols") {
    const std::string source = R"(
.data
.word 0x1234
.word symbol
.extern symbol
)";
    AssemblyResult result;
    std::string error;
    REQUIRE_MESSAGE(assemble(source, result, error), error);
    REQUIRE(result.data.size() == 4);
    CHECK(result.data[0] == 0x34);
    CHECK(result.data[1] == 0x12);
    CHECK(result.data[2] == 0x00);
    CHECK(result.data[3] == 0x00);
    REQUIRE(result.relocations.size() == 1);
    CHECK(result.relocations[0].symbol == "symbol");
    CHECK(result.relocations[0].section == 1);
    CHECK(result.relocations[0].offset == 2);
}
