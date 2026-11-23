#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace krc::backend {

// kr232's register roles (docs/abi-kr232.md, ADR 0057). Nine registers are
// allocatable -- R2 to R10 -- and R11 is never a value: K and I are
// hardware-owned, so code generation must not treat the flag word as restorable.
enum class Kr232RegisterRole {
    Constant,            // R0 is zero, R1 is one
    Argument,            // R2-R5, the first four arguments
    CallerSaved,         // R6, R7: scratch within a call
    CalleeSaved,         // R8, R9
    ThisPointer,         // R10: `this` in a member, scratch in a free function
    Flags,               // R11: Z, N, C, V, I, T, K
    ExceptionFrame,      // R12: EFP, 0 when no trap is active
    FramePointer,        // R13
    ProgramCounter,      // R14
    StackPointer,        // R15
};

struct Kr232RegisterInfo {
    std::string_view name;
    Kr232RegisterRole role;
    bool callee_saved;
    std::int8_t argument_index;  // -1 when the register is not an argument
};

struct Kr232Descriptor {
    unsigned pointer_bits;
    unsigned stack_alignment;         // SP is 4-byte aligned, 8 recommended
    unsigned public_call_alignment;
    std::uint8_t return_register;     // R2
    std::uint8_t sret_register;       // R6, loaded just before CALL
    std::uint8_t this_register;       // R10
    std::uint8_t flags_register;      // R11
    std::uint8_t exception_frame_pointer;  // R12
    std::uint8_t frame_pointer;       // R13
    std::uint8_t program_counter;     // R14
    std::uint8_t stack_pointer;       // R15
    std::array<std::uint8_t, 4> argument_registers;  // R2-R5
    std::array<Kr232RegisterInfo, 16> registers;
};

[[nodiscard]] const Kr232Descriptor& kr232_descriptor();

}  // namespace krc::backend
