// The machine-level representation R1 needs (docs/optimize.md §7, row R1).
//
// The backends emit text instruction by instruction, so the frame layout, the
// argument layout and the register choice are decided by separate mechanisms that
// each consult the emitter's internal state. That is why the two attempts at
// frame-slot reuse in `implementation.md` (`2026-11-19T01:30:20`, `01:43:57`) both
// miscompiled: a pass can see the IR's operands, but not the places `emit_copy`,
// `emit_phi_copies` and the argument paths touch a slot.
//
// This header is the first step of the fix and nothing depends on it yet: a
// function is lowered to a list of machine instructions with operands named
// explicitly, and printed back. When the lowering reproduces the current text
// byte for byte, the representation can start carrying decisions — a virtual
// register per value, a slot plan with one owner — instead of describing them after
// the fact.
//
// Nothing here is target-neutral by accident: the opcodes are KR32's, because that
// is the target R1 is measured on first.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace krc::backend::machine {

// The KR32 instructions the backend emits today, taken from the mnemonic histogram
// of `kr32/libc.c` at `-O1` rather than from the ISA manual: a representation that
// cannot express what is currently emitted is not a step toward replacing it. A
// mnemonic that appears later is added with its lowering.
enum class Opcode {
    Mov,
    Load,
    Load8S,
    Store,
    Store8,
    LoadFloat,    // FLOAD.D
    StoreFloat,   // FSTORE.D
    Lea,
    Add,
    Sub,
    Mul,
    And,
    Or,
    Xor,
    Shl,
    Shr,          // logical
    Sar,
    Cmp,
    Jump,         // JMP
    JumpIfZero,   // JZ
    JumpIfNotZero, // JNZ
    JumpIfLess,   // JLT
    JumpIfGreater,
    JumpIfLessOrEqual,
    JumpIfGreaterOrEqual,
    Call,
    Return,       // RET
    Push,
    Pop,
    AdjustStack,  // ADJSP
    Halt,
    Nop,
};

// What an operand names. `Register` is a physical register once selection is done
// and stays physical through allocation; `Virtual()` is what allocation assigns.
// `FrameOffset` is an address off the frame pointer, `Symbol` an external name.
struct Operand {
    enum class Kind { PhysicalRegister, VirtualRegister, Immediate, FrameOffset, Symbol };

    Kind kind = Kind::Immediate;
    std::int64_t value = 0;   // register number, immediate value, or frame offset
    std::string symbol;       // when kind is Symbol

    [[nodiscard]] static Operand physical(int number);
    [[nodiscard]] static Operand virtual_register(int number);
    [[nodiscard]] static Operand immediate(std::int64_t value);
    [[nodiscard]] static Operand frame_offset(std::int64_t offset);
    [[nodiscard]] static Operand named(std::string symbol);
};

// One machine instruction. `operands` is in emission order, which is what the
// printer and the allocator both need; `comment` carries the trailing `; ...` text
// so that a lowered function can be compared with the current emitter's output
// character for character.
struct Instruction {
    Opcode opcode = Opcode::Nop;
    std::vector<Operand> operands;
    std::string comment;
};

struct Block {
    std::string label;   // the `.L...` label this block starts at, if any
    std::vector<Instruction> instructions;
};

struct Function {
    std::string symbol;
    std::vector<Block> blocks;
};

// Prints one instruction in the assembler's syntax. The printer is the part that
// makes the representation verifiable: lowering is only trustworthy when this
// reproduces the backend's current text exactly.
[[nodiscard]] std::string mnemonic(Opcode opcode);
[[nodiscard]] std::string print_instruction(const Instruction& instruction);
[[nodiscard]] std::string print_function(const Function& function);

}  // namespace krc::backend::machine
