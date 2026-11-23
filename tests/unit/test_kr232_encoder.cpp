#include "doctest.h"

#include "asm/kr232_encoder.h"

#include <string>
#include <vector>

using krc::asm_kr232::encode_compact_branch;
using krc::asm_kr232::encode_compact_register;
using krc::asm_kr232::encode_ld_absolute;
using krc::asm_kr232::encode_ld_immediate;
using krc::asm_kr232::encode_short;
using krc::asm_kr232::encode_st_absolute;
using krc::asm_kr232::EncodedInstruction;
using krc::asm_kr232::ShortOp;

namespace {

void expect_bytes(const std::optional<EncodedInstruction>& encoded,
                  const std::vector<std::uint8_t>& expected) {
    REQUIRE(encoded.has_value());
    REQUIRE(encoded->length == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK(encoded->bytes[i] == expected[i]);
    }
}

}  // namespace

TEST_CASE("KR232 short forms encode as Appendix A shows") {
    std::string error;
    expect_bytes(encode_short(ShortOp::Push, 2, error), {0x02});
    expect_bytes(encode_short(ShortOp::Pop, 2, error), {0x12});
    expect_bytes(encode_short(ShortOp::Inc, 2, error), {0x22});
    expect_bytes(encode_short(ShortOp::Dec, 2, error), {0x32});
    expect_bytes(encode_short(ShortOp::Call, 2, error), {0x42});
    expect_bytes(encode_short(ShortOp::Ret, 0, error), {0x70});
    expect_bytes(encode_short(ShortOp::Nop, 0, error), {0x71});
    expect_bytes(encode_short(ShortOp::Brk, 0, error), {0x72});
    expect_bytes(encode_short(ShortOp::Sys, 0, error), {0x73});
    expect_bytes(encode_short(ShortOp::Ei, 0, error), {0x74});
    expect_bytes(encode_short(ShortOp::Di, 0, error), {0x75});
    expect_bytes(encode_short(ShortOp::Halt, 0, error), {0x76});
    expect_bytes(encode_short(ShortOp::Iret, 0, error), {0x77});
    expect_bytes(encode_short(ShortOp::RdCycle, 0, error), {0x78});
    expect_bytes(encode_short(ShortOp::SysCall, 0, error), {0x79});
}

TEST_CASE("KR232 register forms encode as Appendix A shows") {
    std::string error;
    expect_bytes(encode_ld_immediate(2, 0x1234, error), {0x52, 0x34, 0x12, 0x00, 0x00});
    expect_bytes(encode_ld_absolute(2, 0x1234, error), {0x62, 0x34, 0x12, 0x00, 0x00});
    expect_bytes(encode_st_absolute(2, 0x1234, error), {0xE2, 0x34, 0x12, 0x00, 0x00});
}

TEST_CASE("KR232 compact branches keep the condition out of the ninth bit") {
    std::string error;
    // beq +6 and bra -9 are the two Appendix A vectors.
    expect_bytes(encode_compact_branch(0, 6, error), {0xA0, 0x06});
    expect_bytes(encode_compact_branch(15, -9, error), {0xBF, 0xF7});
    // The condition lives in bits 1-4: beq -9 must not read back as condition 1.
    expect_bytes(encode_compact_branch(0, -9, error), {0xA1, 0xF7});
    expect_bytes(encode_compact_branch(1, -9, error), {0xA3, 0xF7});
    // call (14) and bra (15) with a positive displacement keep bit 0 clear.
    expect_bytes(encode_compact_branch(14, 6, error), {0xBC, 0x06});
    expect_bytes(encode_compact_branch(15, 6, error), {0xBE, 0x06});
}

TEST_CASE("KR232 encoders reject out-of-range operands") {
    std::string error;
    CHECK(!encode_short(ShortOp::Push, 16, error).has_value());
    CHECK(!encode_ld_immediate(16, 0, error).has_value());
    CHECK(!encode_compact_branch(16, 0, error).has_value());
    CHECK(!encode_compact_branch(0, 256, error).has_value());
    CHECK(!encode_compact_branch(0, -257, error).has_value());
}

TEST_CASE("KR232 compact register forms encode as Appendix A shows") {
    std::string error;
    expect_bytes(encode_compact_register(0, 2, 3, error), {0x80, 0x23});   // mov r2,r3
    expect_bytes(encode_compact_register(1, 2, 3, error), {0x81, 0x23});   // add r2,r3
    expect_bytes(encode_compact_register(3, 2, 3, error), {0x83, 0x23});   // sub r2,r3
    expect_bytes(encode_compact_register(5, 2, 3, error), {0x85, 0x23});   // and r2,r3
    expect_bytes(encode_compact_register(14, 2, 3, error), {0x8E, 0x23});  // ld r2,[r3]
    expect_bytes(encode_compact_register(15, 2, 3, error), {0x8F, 0x23});  // st [r3],r2
    expect_bytes(encode_compact_register(16, 2, 3, error), {0x90, 0x23});  // ldbu r2,[r3]
    expect_bytes(encode_compact_register(29, 2, 0, error), {0x9D, 0x20});  // neg r2
    expect_bytes(encode_compact_register(30, 2, 0, error), {0x9E, 0x20});  // not r2
    expect_bytes(encode_compact_register(31, 2, 0, error), {0x9F, 0x20});  // jmp r2
    // The unary forms demand R0 as the second register.
    CHECK(!encode_compact_register(29, 2, 3, error).has_value());
    CHECK(!encode_compact_register(31, 2, 3, error).has_value());
    CHECK(!encode_compact_register(32, 2, 0, error).has_value());
}
