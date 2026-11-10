#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace krc::backend {

enum class Kr32RegisterRole {
    ArgumentOrReturn,
    Argument,
    CallerSaved,
    FramePointer,
    CalleeSaved,
    ThreadPointer,
};

struct Kr32RegisterInfo {
    std::string_view name;
    Kr32RegisterRole role;
    bool callee_saved;
    std::int8_t argument_index;
};

struct Kr32Descriptor {
    unsigned pointer_bits;
    unsigned stack_alignment;
    unsigned public_call_alignment;
    std::uint8_t return_register;
    std::uint8_t frame_pointer;
    std::uint8_t thread_pointer;
    std::array<std::uint8_t, 4> argument_registers;
    std::array<Kr32RegisterInfo, 16> integer_registers;
    std::array<Kr32RegisterInfo, 16> floating_registers;
};

[[nodiscard]] const Kr32Descriptor& kr32_descriptor();

}  // namespace krc::backend
