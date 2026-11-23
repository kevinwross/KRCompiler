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

TEST_CASE("KR232 parser refuses what it cannot encode yet") {
    std::string error;
    CHECK(!parse_instruction("ld r2, #0x1234", error).has_value());
    CHECK(!parse_instruction("", error).has_value());
    CHECK(!parse_instruction("mov r16, r3", error).has_value());
    CHECK(!parse_instruction("frobnicate r2", error).has_value());
}
