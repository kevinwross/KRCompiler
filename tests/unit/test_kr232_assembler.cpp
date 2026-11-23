#include "doctest.h"

#include "asm/kr232_assembler.h"

#include <string>
#include <vector>

using krc::asm_kr232::assemble;
using krc::asm_kr232::AssembledProgram;

TEST_CASE("kr232 assembly places instructions, data and labels") {
    const std::string source =
        ".globl main\n"
        ".text\n"
        "main:\n"
        "  push r2\n"
        "  ld r2, #1\n"
        "  beq done\n"
        "done:\n"
        "  pop r2\n"
        "  sys\n"
        ".data\n"
        "message:\n"
        "  .word 0x12345678\n"
        "  .byte 0xAA, 0xBB\n";

    AssembledProgram program;
    std::string error;
    REQUIRE_MESSAGE(assemble(source, "fixture.s", program, error), error);

    REQUIRE(program.program.sections.size() == 2);
    const std::vector<std::uint8_t>& text = program.program.sections[0].bytes;
    const std::vector<std::uint8_t>& data = program.program.sections[1].bytes;

    // push r2, ld r2,#1, beq (two bytes, patched to skip the pop), pop r2, sys.
    REQUIRE(text.size() == 1 + 3 + 2 + 1 + 1);
    CHECK(text[0] == 0x02);              // push r2
    CHECK(text[1] == 0xC6);              // ld r2, #1
    CHECK(text[2] == 0x20);
    CHECK(text[3] == 0x01);
    CHECK(text[4] == 0xA0);             // beq, displacement patched to +2
    CHECK(text[5] == 0x00);  // beq falls through to the next statement
    CHECK(text[6] == 0x12);             // pop r2
    CHECK(text[7] == 0x73);             // sys

    REQUIRE(data.size() == 6);
    CHECK(data[0] == 0x78);
    CHECK(data[3] == 0x12);
    CHECK(data[4] == 0xAA);
    CHECK(data[5] == 0xBB);

    // Symbols: main in .text at 0, done at 6, message in .data at 0.
    bool saw_main = false;
    bool saw_done = false;
    bool saw_message = false;
    for (const auto& symbol : program.program.symbols) {
        if (symbol.name == "main") {
            saw_main = true;
            CHECK(symbol.section == 0);
            CHECK(symbol.offset == 0);
            CHECK(symbol.global);
        }
        if (symbol.name == "done") {
            saw_done = true;
            CHECK(symbol.section == 0);
            CHECK(symbol.offset == 6);
        }
        if (symbol.name == "message") {
            saw_message = true;
            CHECK(symbol.section == 1);
            CHECK(symbol.offset == 0);
        }
    }
    CHECK(saw_main);
    CHECK(saw_done);
    CHECK(saw_message);

    // One line record per statement, in source order.
    REQUIRE(program.lines.size() == 5);  // one per instruction, not per label
    CHECK(program.lines[0].line == 4);
    CHECK(program.lines[0].offset == 0);
    CHECK(program.lines[0].file == "fixture.s");
}

TEST_CASE("kr232 assembly reports an undefined label and a bad directive") {
    AssembledProgram program;
    std::string error;
    CHECK(!assemble("main:\n  bra nowhere\n", "x.s", program, error));
    CHECK(error.find("nowhere") != std::string::npos);

    CHECK(!assemble(".bogus\n", "x.s", program, error));
    CHECK(!assemble(".text\n  frobnicate r2\n", "x.s", program, error));
}
