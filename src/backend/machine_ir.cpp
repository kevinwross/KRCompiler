#include "backend/machine_ir.h"

#include <cstdio>

namespace krc::backend::machine {

Operand Operand::physical(int number) {
    Operand operand;
    operand.kind = Kind::PhysicalRegister;
    operand.value = number;
    return operand;
}

Operand Operand::virtual_register(int number) {
    Operand operand;
    operand.kind = Kind::VirtualRegister;
    operand.value = number;
    return operand;
}

Operand Operand::immediate(std::int64_t value) {
    Operand operand;
    operand.kind = Kind::Immediate;
    operand.value = value;
    return operand;
}

Operand Operand::frame_offset(std::int64_t offset) {
    Operand operand;
    operand.kind = Kind::FrameOffset;
    operand.value = offset;
    return operand;
}

Operand Operand::named(std::string symbol) {
    Operand operand;
    operand.kind = Kind::Symbol;
    operand.symbol = std::move(symbol);
    return operand;
}

std::string mnemonic(Opcode opcode) {
    switch (opcode) {
    case Opcode::Mov:
        return "MOV";
    case Opcode::Load:
        return "LOAD";
    case Opcode::Load8S:
        return "LOAD8S";
    case Opcode::Store:
        return "STORE";
    case Opcode::Store8:
        return "STORE8";
    case Opcode::LoadFloat:
        return "FLOAD.D";
    case Opcode::StoreFloat:
        return "FSTORE.D";
    case Opcode::Lea:
        return "LEA";
    case Opcode::Add:
        return "ADD";
    case Opcode::Sub:
        return "SUB";
    case Opcode::Mul:
        return "MUL";
    case Opcode::And:
        return "AND";
    case Opcode::Or:
        return "OR";
    case Opcode::Xor:
        return "XOR";
    case Opcode::Shl:
        return "SHL";
    case Opcode::Shr:
        return "SHR";
    case Opcode::Sar:
        return "SAR";
    case Opcode::Cmp:
        return "CMP";
    case Opcode::Jump:
        return "JMP";
    case Opcode::JumpIfZero:
        return "JZ";
    case Opcode::JumpIfNotZero:
        return "JNZ";
    case Opcode::JumpIfLess:
        return "JLT";
    case Opcode::JumpIfGreater:
        return "JGT";
    case Opcode::JumpIfLessOrEqual:
        return "JLE";
    case Opcode::JumpIfGreaterOrEqual:
        return "JGE";
    case Opcode::Call:
        return "CALL";
    case Opcode::Return:
        return "RET";
    case Opcode::Push:
        return "PUSH";
    case Opcode::Pop:
        return "POP";
    case Opcode::AdjustStack:
        return "ADJSP";
    case Opcode::Halt:
        return "HALT";
    case Opcode::Nop:
        return "NOP";
    }
    return "NOP";
}

namespace {

std::string format_operand(const Operand& operand) {
    switch (operand.kind) {
    case Operand::Kind::PhysicalRegister:
        return "R" + std::to_string(operand.value);
    case Operand::Kind::VirtualRegister:
        // A virtual register is printed where a physical one would go, so that a
        // lowered function is comparable to the emitter's output before allocation
        // has run. `%` keeps it visually distinct and cannot collide with `R<n>`.
        return "%v" + std::to_string(operand.value);
    case Operand::Kind::Immediate:
        return "#" + std::to_string(operand.value);
    case Operand::Kind::FrameOffset:
        // The backend prints frame addresses as `[R7 + k]`, with a sign that is
        // only present for a negative offset.
        return "[R7 " + std::string(operand.value < 0 ? "- " : "+ ") +
               std::to_string(operand.value < 0 ? -operand.value : operand.value) + "]";
    case Operand::Kind::Symbol:
        return operand.symbol;
    }
    return {};
}

}  // namespace

std::string print_instruction(const Instruction& instruction) {
    std::string text = "        ";
    text += mnemonic(instruction.opcode);
    for (std::size_t index = 0; index < instruction.operands.size(); ++index) {
        text += index == 0 ? " " : ", ";
        text += format_operand(instruction.operands[index]);
    }
    if (!instruction.comment.empty()) {
        text += " ; ";
        text += instruction.comment;
    }
    text += "\n";
    return text;
}

std::string print_function(const Function& function) {
    std::string text;
    text += function.symbol;
    text += ":\n";
    for (const auto& block : function.blocks) {
        if (!block.label.empty()) {
            text += block.label;
            text += ":\n";
        }
        for (const auto& instruction : block.instructions) {
            text += print_instruction(instruction);
        }
    }
    return text;
}

}  // namespace krc::backend::machine
