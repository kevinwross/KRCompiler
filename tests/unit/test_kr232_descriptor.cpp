#include "doctest.h"
#include "backend/kr232_descriptor.h"

using krc::backend::Kr232RegisterRole;
using krc::backend::kr232_descriptor;

namespace {

const krc::backend::Kr232RegisterInfo& reg(unsigned index) {
    return kr232_descriptor().registers.at(index);
}

}  // namespace

TEST_CASE("KR232 descriptor names every register role") {
    const auto& descriptor = kr232_descriptor();
    CHECK(descriptor.pointer_bits == 32);
    CHECK(descriptor.stack_alignment == 4);
    CHECK(descriptor.public_call_alignment == 4);
    CHECK(descriptor.return_register == 2);
    CHECK(descriptor.sret_register == 6);
    CHECK(descriptor.this_register == 10);
    CHECK(descriptor.flags_register == 11);
    CHECK(descriptor.exception_frame_pointer == 12);
    CHECK(descriptor.frame_pointer == 13);
    CHECK(descriptor.program_counter == 14);
    CHECK(descriptor.stack_pointer == 15);
    CHECK(descriptor.argument_registers == std::array<std::uint8_t, 4>{2, 3, 4, 5});
}

TEST_CASE("KR232 descriptor pins each register's role and index") {
    CHECK(reg(0).role == Kr232RegisterRole::Constant);
    CHECK(reg(1).role == Kr232RegisterRole::Constant);
    for (unsigned index = 2; index <= 5; ++index) {
        CHECK(reg(index).role == Kr232RegisterRole::Argument);
        CHECK(reg(index).argument_index == static_cast<int>(index) - 2);
        CHECK_FALSE(reg(index).callee_saved);
    }
    CHECK(reg(6).role == Kr232RegisterRole::CallerSaved);
    CHECK(reg(7).role == Kr232RegisterRole::CallerSaved);
    CHECK(reg(8).role == Kr232RegisterRole::CalleeSaved);
    CHECK(reg(8).callee_saved);
    CHECK(reg(9).role == Kr232RegisterRole::CalleeSaved);
    CHECK(reg(9).callee_saved);
    CHECK(reg(10).role == Kr232RegisterRole::ThisPointer);
    CHECK(reg(11).role == Kr232RegisterRole::Flags);
    CHECK(reg(12).role == Kr232RegisterRole::ExceptionFrame);
    CHECK(reg(13).role == Kr232RegisterRole::FramePointer);
    CHECK(reg(14).role == Kr232RegisterRole::ProgramCounter);
    CHECK(reg(15).role == Kr232RegisterRole::StackPointer);
    for (unsigned index = 0; index < 16; ++index) {
        CHECK(!reg(index).name.empty());
    }
    // Only R8 and R9 survive a call.
    for (unsigned index = 0; index < 16; ++index) {
        if (index != 8 && index != 9) {
            CHECK_FALSE(reg(index).callee_saved);
        }
    }
}
