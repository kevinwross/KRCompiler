#include "kr232_descriptor.h"

namespace krc::backend {
namespace {

using R = Kr232RegisterRole;

constexpr Kr232Descriptor kKr232Descriptor{
    32,  // pointer_bits: Ilp32
    4,   // stack_alignment: SP is 4-byte aligned (docs/abi-kr232.md)
    4,   // public_call_alignment
    2,   // return_register
    6,   // sret_register
    10,  // this_register
    11,  // flags_register
    12,  // exception_frame_pointer
    13,  // frame_pointer
    14,  // program_counter
    15,  // stack_pointer
    {2, 3, 4, 5},
    {{
        {"r0", R::Constant, false, -1},
        {"r1", R::Constant, false, -1},
        {"r2", R::Argument, false, 0},
        {"r3", R::Argument, false, 1},
        {"r4", R::Argument, false, 2},
        {"r5", R::Argument, false, 3},
        {"r6", R::CallerSaved, false, -1},
        {"r7", R::CallerSaved, false, -1},
        {"r8", R::CalleeSaved, true, -1},
        {"r9", R::CalleeSaved, true, -1},
        {"r10", R::ThisPointer, false, -1},
        {"r11", R::Flags, false, -1},
        {"r12", R::ExceptionFrame, false, -1},
        {"r13", R::FramePointer, false, -1},
        {"r14", R::ProgramCounter, false, -1},
        {"r15", R::StackPointer, false, -1},
    }},
};

}  // namespace

const Kr232Descriptor& kr232_descriptor() { return kKr232Descriptor; }

}  // namespace krc::backend
