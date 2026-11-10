#include "asm/kr16_encoder.h"

#include <cstdint>
#include <string>

#include "doctest.h"

using namespace krc::asmkr16;

TEST_CASE("kr16 encoder reproduces the ISA golden encodings") {
    std::string error;

    const auto move16 = encode_x(XOp::MoveImmediate, 0, 0, 0x1234, error);
    REQUIRE(move16.has_value());
    CHECK(move16->words[0] == 0xF000);
    CHECK(move16->words[1] == 0x1234);
    CHECK(move16->count == 2);

    const auto load = encode_m(MOpcode::Load, 0, 1, 4, error);
    REQUIRE(load.has_value());
    CHECK(load->words[0] == 0x2044);

    const auto store = encode_m(MOpcode::Store, 0, 1, 4, error);
    REQUIRE(store.has_value());
    CHECK(store->words[0] == 0x3044);

    const auto branch = encode_b(Condition::Eq, 1, error);
    REQUIRE(branch.has_value());
    CHECK(branch->words[0] == 0x7001);

    const auto jump = encode_c_relative(CSubop::JumpRelative, 2, error);
    REQUIRE(jump.has_value());
    CHECK(jump->words[0] == 0x8000);
    CHECK(jump->words[1] == 2);
    CHECK(jump->count == 2);

    const auto call = encode_c(CSubop::CallRegister, 1, 0, error);
    REQUIRE(call.has_value());
    CHECK(call->words[0] == 0x8320);

    const auto ret = encode_c(CSubop::Return, 0, 0, error);
    REQUIRE(ret.has_value());
    CHECK(ret->words[0] == 0x8400);

    const auto system = encode_c(CSubop::System, 0, 0, error);
    REQUIRE(system.has_value());
    CHECK(system->words[0] == 0x8F00);
}

TEST_CASE("kr16 encoder covers ALU, shift, bit, and indexed forms") {
    std::string error;

    const auto add = encode_r(1, 2, RFunction::Add, error);
    REQUIRE(add.has_value());
    CHECK(add->words[0] == 0x0280);

    const auto add_immediate = encode_i(1, IFunction::Add, -1, error);
    REQUIRE(add_immediate.has_value());
    CHECK(add_immediate->words[0] == 0x123F);

    const auto move_byte = encode_m8(0, 0x7F, false, error);
    REQUIRE(move_byte.has_value());
    CHECK(move_byte->words[0] == 0x907F);

    const auto move_signed = encode_m8(0, 0xFF, true, error);
    REQUIRE(move_signed.has_value());
    CHECK(move_signed->words[0] == 0x91FF);

    const auto shift = encode_shift(1, Shift::Shl, 3, error);
    REQUIRE(shift.has_value());
    CHECK(shift->words[0] == 0xB203);

    const auto bit = encode_bit(1, BitOperation::Bset, 7, error);
    REQUIRE(bit.has_value());
    CHECK(bit->words[0] == 0xC247);

    const auto indexed = encode_x(XOp::LoadIndexed, 0, 1, 0x0102, error);
    REQUIRE(indexed.has_value());
    CHECK(indexed->words[0] == 0xFD01);
    CHECK(indexed->words[1] == 0x0102);
}

TEST_CASE("kr16 encoder rejects out-of-range fields") {
    std::string error;
    CHECK_FALSE(encode_r(8, 0, RFunction::Add, error).has_value());
    CHECK_FALSE(encode_i(0, IFunction::Add, 32, error).has_value());
    CHECK_FALSE(encode_m(MOpcode::Load, 0, 1, 32, error).has_value());
    CHECK_FALSE(encode_b(Condition::Eq, 128, error).has_value());
    CHECK_FALSE(encode_shift(0, Shift::Shl, 16, error).has_value());
    CHECK_FALSE(encode_bit(0, BitOperation::Btst, 16, error).has_value());
    CHECK_FALSE(encode_c(CSubop::Trap, 0, 120, error).has_value());
    CHECK_FALSE(encode_r(1, 0, RFunction::MulS, error).has_value());
}
