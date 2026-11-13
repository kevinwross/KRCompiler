#include <cstdint>
#include <ostream>
#include <string>

#include "asm/kr32_encoder.h"
#include "backend/kr32_descriptor.h"
#include "doctest.h"

using krc::asmkr32::AddressingMode;
using krc::asmkr32::Condition;
using krc::asmkr32::EncodedInstruction;
using krc::asmkr32::Opcode;

namespace {

EncodedInstruction encode_or_fail(Opcode opcode, std::uint8_t rd, std::uint8_t rs1,
                                  std::uint8_t field, bool literal, std::int32_t immediate,
                                  bool extension = false, std::uint32_t extension_word = 0) {
    std::string error;
    const auto encoded = krc::asmkr32::encode(opcode, rd, rs1, field, literal, immediate,
                                              extension, extension_word, error);
    REQUIRE_MESSAGE(encoded.has_value(), error);
    return *encoded;
}

}  // namespace

TEST_CASE("KR32 encoder reproduces the golden instruction words") {
    CHECK(encode_or_fail(Opcode::Mov, 1, 0, 0, true, 42).words[0] == 0x0020082Au);
    CHECK(encode_or_fail(Opcode::Add, 3, 1, 2, false, 0).words[0] == 0x40624000u);
    CHECK(encode_or_fail(Opcode::Slt, 1, 2, 3, false, 0).words[0] == 0x30246000u);
    CHECK(encode_or_fail(Opcode::Sltu, 1, 2, 3, false, 0).words[0] == 0x32246000u);
    CHECK(encode_or_fail(Opcode::MulH, 1, 2, 3, false, 0).words[0] == 0x46246000u);
    CHECK(encode_or_fail(Opcode::MulHu, 1, 2, 3, false, 0).words[0] == 0x48246000u);
    CHECK(encode_or_fail(Opcode::Load, 4, 5, 3, true, 8).words[0] == 0x028A6808u);
    CHECK(encode_or_fail(Opcode::Load8U, 4, 5, 3, true, 8).words[0] == 0x068A6808u);
    CHECK(encode_or_fail(Opcode::Store, 6, 7, 2, true, 0).words[0] == 0x04CE4800u);
    CHECK(encode_or_fail(Opcode::Jmp, 0, 0, 0, true, 0, true, 0x00400000u).words[0] ==
          0x70001800u);
    CHECK(encode_or_fail(Opcode::Call, 0, 0, 0, true, 0, true, 0x00400000u).words[0] ==
          0x74001800u);
    CHECK(encode_or_fail(Opcode::Jcond, 0, 7, 0, false, 0).words[0] == 0x720E0000u);
    CHECK(encode_or_fail(Opcode::Ret, 0, 0, 0, false, 0).words[0] == 0x76000000u);
    CHECK(encode_or_fail(Opcode::Halt, 0, 0, 0, false, 0).words[0] == 0x86000000u);
    CHECK(encode_or_fail(Opcode::Adjsp, 0, 0, 0, true, -16).words[0] == 0x7C000FF0u);
    CHECK(encode_or_fail(Opcode::PushR, 0, 0, 0, false, 0).words[0] == 0x9A000000u);
    CHECK(encode_or_fail(Opcode::PopR, 0, 0, 0, false, 0).words[0] == 0x9C000000u);
    CHECK(encode_or_fail(Opcode::PushFP, 0, 0, 0, false, 0).words[0] == 0x9E000000u);
    CHECK(encode_or_fail(Opcode::PopFP, 0, 0, 0, false, 0).words[0] == 0x14000000u);
}

TEST_CASE("KR32 encoder emits and validates extension words") {
    const auto encoded =
        encode_or_fail(Opcode::Call, 0, 0, 0, true, 0, true, 0x00400000u);
    CHECK(encoded.count == 2);
    CHECK(encoded.words[1] == 0x00400000u);

    std::string error;
    const auto too_large = krc::asmkr32::encode(Opcode::Mov, 1, 0, 0, true, 2048, false, 0,
                                                error);
    CHECK_FALSE(too_large.has_value());
    CHECK_FALSE(error.empty());

    error.clear();
    const auto bad_register =
        krc::asmkr32::encode(Opcode::Mov, 16, 0, 0, true, 0, false, 0, error);
    CHECK_FALSE(bad_register.has_value());
    CHECK_FALSE(error.empty());
}

TEST_CASE("KR32 descriptor records the C ABI register roles") {
    const auto& descriptor = krc::backend::kr32_descriptor();
    CHECK(descriptor.pointer_bits == 32);
    CHECK(descriptor.stack_alignment == 4);
    CHECK(descriptor.public_call_alignment == 16);
    CHECK(descriptor.return_register == 0);
    CHECK(descriptor.frame_pointer == 7);
    CHECK(descriptor.thread_pointer == 11);
    CHECK(descriptor.argument_registers[0] == 0);
    CHECK(descriptor.argument_registers[3] == 3);

    CHECK(descriptor.integer_registers[0].name == "R0");
    CHECK(descriptor.integer_registers[0].role ==
          krc::backend::Kr32RegisterRole::ArgumentOrReturn);
    CHECK(descriptor.integer_registers[7].role == krc::backend::Kr32RegisterRole::FramePointer);
    CHECK(descriptor.integer_registers[7].callee_saved);
    CHECK(descriptor.integer_registers[8].role == krc::backend::Kr32RegisterRole::CalleeSaved);
    CHECK(descriptor.integer_registers[11].role ==
          krc::backend::Kr32RegisterRole::ThreadPointer);
    CHECK_FALSE(descriptor.integer_registers[12].callee_saved);

    CHECK(descriptor.floating_registers[0].name == "F0");
    CHECK(descriptor.floating_registers[7].role == krc::backend::Kr32RegisterRole::Argument);
    CHECK(descriptor.floating_registers[8].callee_saved);
}
