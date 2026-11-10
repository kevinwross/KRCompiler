#include "backend/kr32_descriptor.h"

namespace krc::backend {

const Kr32Descriptor& kr32_descriptor() {
    static const Kr32Descriptor descriptor{
        .pointer_bits = 32,
        .stack_alignment = 4,
        .public_call_alignment = 16,
        .return_register = 0,
        .frame_pointer = 7,
        .thread_pointer = 11,
        .argument_registers = {0, 1, 2, 3},
        .integer_registers = {{
            {"R0", Kr32RegisterRole::ArgumentOrReturn, false, 0},
            {"R1", Kr32RegisterRole::Argument, false, 1},
            {"R2", Kr32RegisterRole::Argument, false, 2},
            {"R3", Kr32RegisterRole::Argument, false, 3},
            {"R4", Kr32RegisterRole::CallerSaved, false, -1},
            {"R5", Kr32RegisterRole::CallerSaved, false, -1},
            {"R6", Kr32RegisterRole::CallerSaved, false, -1},
            {"R7", Kr32RegisterRole::FramePointer, true, -1},
            {"R8", Kr32RegisterRole::CalleeSaved, true, -1},
            {"R9", Kr32RegisterRole::CalleeSaved, true, -1},
            {"R10", Kr32RegisterRole::CalleeSaved, true, -1},
            {"R11", Kr32RegisterRole::ThreadPointer, true, -1},
            {"R12", Kr32RegisterRole::CallerSaved, false, -1},
            {"R13", Kr32RegisterRole::CallerSaved, false, -1},
            {"R14", Kr32RegisterRole::CallerSaved, false, -1},
            {"R15", Kr32RegisterRole::CallerSaved, false, -1},
        }},
        .floating_registers = {{
            {"F0", Kr32RegisterRole::ArgumentOrReturn, false, 0},
            {"F1", Kr32RegisterRole::Argument, false, 1},
            {"F2", Kr32RegisterRole::Argument, false, 2},
            {"F3", Kr32RegisterRole::Argument, false, 3},
            {"F4", Kr32RegisterRole::Argument, false, 4},
            {"F5", Kr32RegisterRole::Argument, false, 5},
            {"F6", Kr32RegisterRole::Argument, false, 6},
            {"F7", Kr32RegisterRole::Argument, false, 7},
            {"F8", Kr32RegisterRole::CalleeSaved, true, -1},
            {"F9", Kr32RegisterRole::CalleeSaved, true, -1},
            {"F10", Kr32RegisterRole::CalleeSaved, true, -1},
            {"F11", Kr32RegisterRole::CalleeSaved, true, -1},
            {"F12", Kr32RegisterRole::CalleeSaved, true, -1},
            {"F13", Kr32RegisterRole::CalleeSaved, true, -1},
            {"F14", Kr32RegisterRole::CalleeSaved, true, -1},
            {"F15", Kr32RegisterRole::CalleeSaved, true, -1},
        }},
    };
    return descriptor;
}

}  // namespace krc::backend
