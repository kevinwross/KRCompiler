#include "doctest.h"

#include "asm/kr232_parser.h"

#include <string>
#include <vector>

using krc::asm_kr232::parse_instruction;

namespace {

void expect_parse(const std::string& text, const std::vector<std::uint8_t>& bytes) {
    std::string error;
    const auto encoded = parse_instruction(text, error);
    REQUIRE_MESSAGE(encoded.has_value(), text << ": " << error);
    REQUIRE(encoded->length == bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        INFO(text);
        CHECK(encoded->bytes[i] == bytes[i]);
    }
}

}  // namespace

TEST_CASE("KR232 parser reads the zero-operand and register-only forms") {
    expect_parse("ret", {0x70});
    expect_parse("NOP", {0x71});
    expect_parse("brk", {0x72});
    expect_parse("syscall", {0x79});
    expect_parse("rdcycle", {0x78});
    expect_parse("push r2", {0x02});
    expect_parse("pop r2", {0x12});
    expect_parse("inc r2", {0x22});
    expect_parse("call r2", {0x42});
    expect_parse("mov r2, r3", {0x80, 0x23});
    expect_parse("add r2,r3", {0x81, 0x23});
    expect_parse("sub r2, r3", {0x83, 0x23});
    expect_parse("and r2, r3", {0x85, 0x23});
    expect_parse("mulh r2, r3", {0x8A, 0x23});
    expect_parse("neg r2", {0x9D, 0x20});
    expect_parse("not r2", {0x9E, 0x20});
    // Upper case and extra spaces are the same instruction.
    expect_parse("  ADD   R2 ,  R3  ", {0x81, 0x23});
}

TEST_CASE("KR232 parser reads register-with-immediate text") {
    expect_parse("ld r2, #5", {0xC6, 0x20, 0x05});
    expect_parse("ld r2, #-1", {0xC5, 0x20, 0xFF});
    expect_parse("ld r2, #0x1234", {0x52, 0x34, 0x12, 0x00, 0x00});
    expect_parse("add r2, #7", {0xC7, 0x20, 0x07});
    expect_parse("sub r2, #7", {0xC8, 0x20, 0x07});
    expect_parse("cmp r2, #7", {0xC9, 0x20, 0x07});
    expect_parse("and r2, #0xFF", {0xCB, 0x20, 0xFF});
    expect_parse("shl r2, #3", {0xD4, 0x20, 0x03});
    expect_parse("push #0x0300", {0xD7, 0x00, 0x03});
    expect_parse("pop #0x0300", {0xD8, 0x00, 0x03});
    expect_parse("leave #0", {0xDC, 0x00, 0x00});

    std::string error;
    CHECK(!parse_instruction("shl r2, #32", error).has_value());
    CHECK(!parse_instruction("shl r2, #32", error).has_value());
}

TEST_CASE("KR232 parser reads bracketed addresses") {
    expect_parse("ld r2, [r3]", {0x8E, 0x23});
    expect_parse("st [r3], r2", {0x8F, 0x23});
    expect_parse("ld r4, [r2+8]", {0xC0, 0x42, 0x08});
    expect_parse("st [r2+8], r4", {0xC1, 0x42, 0x08});
    expect_parse("ldbu r2, [r3]", {0x90, 0x23});
    expect_parse("ld r2, [r3]+", {0x93, 0x23});
    expect_parse("ld r2, -[r3]", {0x98, 0x23});
    expect_parse("ldbu r2, [0x1234]", {0xF1, 0x02, 0x34, 0x12, 0x00, 0x00});
    expect_parse("ld r2, [0x1234]", {0x62, 0x34, 0x12, 0x00, 0x00});
    expect_parse("st [0x1234], r2", {0xE2, 0x34, 0x12, 0x00, 0x00});
    expect_parse("lea r2, [r3+8]", {0xCE, 0x23, 0x08});
    expect_parse("lea r2, [pc+0x40]", {0xF1, 0x32, 0x40, 0x00, 0x00, 0x00});
    expect_parse("lea r2, [sp+8]", {0xF1, 0x42, 0x08, 0x00, 0x00, 0x00});
}

TEST_CASE("KR232 parser reads branches and control transfers") {
    expect_parse("beq +6", {0xA0, 0x06});
    expect_parse("bne +6", {0xA2, 0x06});
    expect_parse("beq -9", {0xA1, 0xF7});
    expect_parse("bge +6", {0xB4, 0x06});
    expect_parse("ble -9", {0xBB, 0xF7});
    expect_parse("bra -9", {0xBF, 0xF7});
    expect_parse("bra +6", {0xBE, 0x06});
    expect_parse("call +6", {0xD0, 0x06, 0x00});
    expect_parse("jmp +6", {0xCF, 0x06, 0x00});
    expect_parse("jmp r2", {0x9F, 0x20});
    expect_parse("call r2", {0x42});
    expect_parse("jmp 0x1234", {0xF9, 0x00, 0x34, 0x12, 0x00, 0x00});
    expect_parse("call 0x1234", {0xF9, 0x10, 0x34, 0x12, 0x00, 0x00});
}

TEST_CASE("KR232 parser reads the floating-point text") {
    expect_parse("fadd f1, f2", {0xF3, 0x06});
    expect_parse("fsub f1, f2", {0xF3, 0x16});
    expect_parse("fmov f1, f2", {0xF3, 0x76});
    expect_parse("fcmp f0, f1", {0xF3, 0x81});
    expect_parse("fneg f0", {0xF3, 0x40});
    expect_parse("fsqrt f1", {0xF3, 0x64});
    expect_parse("fld f1, [r2+8]", {0xF4, 0x05, 0x20, 0x08});
    expect_parse("fst f1, [r2]", {0xF4, 0x14, 0x20});
    expect_parse("flds f1, [r2]+", {0xF4, 0x26, 0x20});
    expect_parse("fsts f1, -[r2]", {0xF4, 0x37, 0x20});
    expect_parse("fld f1, [r2+0x400]", {0xFA, 0x04, 0x20, 0x00, 0x04, 0x00, 0x00});
    expect_parse("fld f1, [pc+0x40]", {0xFA, 0x05, 0x00, 0x40, 0x00, 0x00, 0x00});
    expect_parse("i2f f1, r2", {0xF5, 0x04, 0x20});
    expect_parse("f2u r2, f1", {0xF5, 0x34, 0x20});
    expect_parse("fpush f1", {0xF6, 0x01});
    expect_parse("fpop f2", {0xF6, 0x06});
}

TEST_CASE("KR232 parser refuses what it cannot encode yet") {
    std::string error;
    CHECK(!parse_instruction("fld f1, [0x1234]", error).has_value());
    CHECK(!parse_instruction("", error).has_value());
    CHECK(!parse_instruction("mov r16, r3", error).has_value());
    CHECK(!parse_instruction("frobnicate r2", error).has_value());
}
