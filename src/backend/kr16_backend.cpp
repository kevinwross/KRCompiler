#include "backend/kr16_backend.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace krc::backend {

namespace {

constexpr std::uint32_t kSlotSize = 2;

std::string label_for(const ir::BasicBlock& block) {
    return ".L" + block.parent->name + "." + block.name;
}

std::string condition_for(ir::ICmpPredicate predicate) {
    switch (predicate) {
    case ir::ICmpPredicate::Eq:
        return "BEQ";
    case ir::ICmpPredicate::Ne:
        return "BNE";
    case ir::ICmpPredicate::Ult:
        return "BCC";
    case ir::ICmpPredicate::Ule:
        return "BLS";
    case ir::ICmpPredicate::Ugt:
        return "BHI";
    case ir::ICmpPredicate::Uge:
        return "BCS";
    case ir::ICmpPredicate::Slt:
        return "BLT";
    case ir::ICmpPredicate::Sle:
        return "BLE";
    case ir::ICmpPredicate::Sgt:
        return "BGT";
    case ir::ICmpPredicate::Sge:
        return "BGE";
    }
    return "BEQ";
}

}  // namespace

Kr16Backend::Kr16Backend(const ir::Module& module,
                         const target::TargetProfile& profile)
    : module_(module), profile_(profile) {}

Kr16Backend::FrameLayout Kr16Backend::layout_function(
    const ir::Function& function, std::string& error) const {
    FrameLayout layout;
    std::uint32_t offset = 0;
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block->instructions) {
            if (instruction->opcode == ir::Opcode::Alloca) {
                offset = (offset + 1) & ~1u;
                layout.allocas[instruction.get()] = offset;
                offset += kSlotSize;
                layout.slots[instruction.get()] = layout.allocas[instruction.get()];
                continue;
            }
            if (instruction->type && !ir::is_void(instruction->type)) {
                offset = (offset + 1) & ~1u;
                layout.slots[instruction.get()] = offset;
                offset += kSlotSize;
            }
        }
    }
    for (const auto& parameter : function.parameters) {
        offset = (offset + 1) & ~1u;
        layout.slots[parameter.get()] = offset;
        offset += kSlotSize;
    }
    layout.frame_size = (offset + 1) & ~1u;
    if (layout.frame_size > 0x7FFF) {
        error = "KR16 frame exceeds the signed 16-bit displacement range";
    }
    return layout;
}

void Kr16Backend::emit_function(std::string& out,
                                const ir::Function& function,
                                const FrameLayout& layout,
                                std::string& error) const {
    const auto slot = [&](const ir::Value* value) -> std::uint32_t {
        const auto found = layout.slots.find(value);
        if (found == layout.slots.end()) {
            error = "KR16 value '" + value->name + "' has no frame slot";
            return 0;
        }
        return found->second;
    };
    const auto load = [&](const ir::Value* value, std::string_view reg) {
        if (!value) {
            out += "    MOV " + std::string(reg) + ", #0\n";
            return;
        }
        if (value->kind == ir::ValueKind::ConstantInt) {
            const auto constant =
                static_cast<const ir::ConstantInt*>(value)->value;
            out += "    MOV " + std::string(reg) + ", #" +
                   std::to_string(constant) + "\n";
            return;
        }
        if (value->kind == ir::ValueKind::ConstantNull) {
            out += "    MOV " + std::string(reg) + ", #0\n";
            return;
        }
        if (value->kind == ir::ValueKind::Instruction) {
            const auto* instruction =
                static_cast<const ir::Instruction*>(value);
            if (instruction->opcode == ir::Opcode::Alloca) {
                out += "    LEA " + std::string(reg) + ", [SP+" +
                       std::to_string(slot(value)) + "]\n";
                return;
            }
        }
        out += "    LD " + std::string(reg) + ", [SP+" +
               std::to_string(slot(value)) + "]\n";
    };
    const auto store = [&](const ir::Instruction& instruction,
                           std::string_view reg) {
        out += "    ST [SP+" + std::to_string(slot(&instruction)) + "], " +
               std::string(reg) + "\n";
    };

    if (function.linkage == ir::Linkage::External) {
        out += ".global " + function.name + "\n";
    }
    out += function.name + ":\n";
    out += "    PUSH R4\n";
    out += "    MOV R4, SP\n";
    out += "    LEA SP, [R4-" + std::to_string(layout.frame_size) + "]\n";
    static const char* arguments[] = {"R1", "R2", "R3"};
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        if (index >= 3) {
            error = "KR16 supports at most three register parameters";
            return;
        }
        out += "    ST [SP+" +
               std::to_string(slot(function.parameters[index].get())) + "], " +
               arguments[index] + "\n";
    }

    std::size_t synthetic_label = 0;
    for (const auto& block : function.blocks) {
        out += label_for(*block) + ":\n";
        for (const auto& instruction : block->instructions) {
            switch (instruction->opcode) {
            case ir::Opcode::Alloca:
                break;
            case ir::Opcode::Load:
                load(instruction->operands[0], "R0");
                out += "    LD R1, [R0]\n";
                store(*instruction, "R1");
                break;
            case ir::Opcode::Store:
                load(instruction->operands[0], "R0");
                load(instruction->operands[1], "R1");
                out += "    ST [R1], R0\n";
                break;
            case ir::Opcode::Add:
            case ir::Opcode::Sub:
            case ir::Opcode::And:
            case ir::Opcode::Or:
            case ir::Opcode::Xor: {
                load(instruction->operands[0], "R0");
                load(instruction->operands[1], "R1");
                const auto mnemonic =
                    instruction->opcode == ir::Opcode::Add
                        ? "ADD"
                        : (instruction->opcode == ir::Opcode::Sub
                               ? "SUB"
                               : (instruction->opcode == ir::Opcode::And
                                      ? "AND"
                                      : (instruction->opcode == ir::Opcode::Or
                                             ? "OR"
                                             : "XOR")));
                out += "    " + std::string(mnemonic) + " R0, R1\n";
                store(*instruction, "R0");
                break;
            }
            case ir::Opcode::ICmp: {
                load(instruction->operands[0], "R0");
                load(instruction->operands[1], "R1");
                out += "    CMP R0, R1\n";
                const auto true_label =
                    ".Lcmp.true." + std::to_string(synthetic_label);
                const auto end_label =
                    ".Lcmp.end." + std::to_string(synthetic_label);
                ++synthetic_label;
                out += "    " + condition_for(instruction->icmp_predicate) + " " +
                       true_label + "\n";
                out += "    MOV R0, #0\n";
                out += "    BRA " + end_label + "\n";
                out += true_label + ":\n";
                out += "    MOV R0, #1\n";
                out += end_label + ":\n";
                store(*instruction, "R0");
                break;
            }
            case ir::Opcode::BitCast:
                if (!instruction->operands.empty() &&
                    instruction->operands[0] &&
                    instruction->operands[0]->kind == ir::ValueKind::Function) {
                    out += "    MOV R0, #0\n";
                    store(*instruction, "R0");
                    break;
                }
                load(instruction->operands[0], "R0");
                store(*instruction, "R0");
                break;
            case ir::Opcode::PtrToInt:
            case ir::Opcode::IntToPtr:
            case ir::Opcode::ZExt:
            case ir::Opcode::SExt:
            case ir::Opcode::Trunc:
                load(instruction->operands[0], "R0");
                store(*instruction, "R0");
                break;
            case ir::Opcode::Br:
                if (instruction->successors.empty()) {
                    error = "KR16 branch has no destination";
                    return;
                }
                out += "    BRA " + label_for(*instruction->successors[0]) +
                       "\n";
                break;
            case ir::Opcode::CondBr: {
                if (instruction->successors.size() < 2) {
                    error = "KR16 conditional branch needs two destinations";
                    return;
                }
                load(instruction->operands[0], "R0");
                out += "    CMP R0, #0\n";
                out += "    BNE " + label_for(*instruction->successors[0]) +
                       "\n";
                out += "    BRA " + label_for(*instruction->successors[1]) +
                       "\n";
                break;
            }
            case ir::Opcode::Call: {
                for (std::size_t index = 1;
                     index < instruction->operands.size(); ++index) {
                    if (index > 3) {
                        error = "KR16 supports at most three call arguments";
                        return;
                    }
                    load(instruction->operands[index],
                         arguments[index - 1]);
                }
                const auto* callee = instruction->operands[0];
                if (callee->kind == ir::ValueKind::Function) {
                    out += "    CALL " + callee->name + "\n";
                } else {
                    error = "KR16 indirect calls are not implemented yet";
                    return;
                }
                if (instruction->type && !ir::is_void(instruction->type)) {
                    store(*instruction, "R0");
                }
                break;
            }
            case ir::Opcode::Ret:
                if (!instruction->operands.empty() &&
                    instruction->operands[0]) {
                    load(instruction->operands[0], "R0");
                }
                out += "    BRA .L" + function.name + ".ret\n";
                break;
            default:
                error = "KR16 backend does not support opcode " +
                        std::string(ir::opcode_name(instruction->opcode));
                return;
            }
        }
    }
    out += ".L" + function.name + ".ret:\n";
    out += "    MOV SP, R4\n";
    out += "    POP R4\n";
    out += "    RET\n";
}

std::optional<std::string> Kr16Backend::emit(std::string& error) const {
    static_cast<void>(profile_);
    std::string out = "; KR16 assembly generated by krc\n";
    out += ".text\n";
    for (const auto& function : module_.functions()) {
        if (function->declaration) {
            out += ".extern " + function->name + "\n";
            continue;
        }
        for (const auto& parameter : function->parameters) {
            if (parameter->type &&
                (parameter->type->kind == ir::TypeKind::Struct ||
                 ir::is_float(parameter->type))) {
                error = "KR16 aggregate and floating-point parameters are not "
                        "implemented";
                return std::nullopt;
            }
        }
        const auto layout = layout_function(*function, error);
        if (!error.empty()) {
            return std::nullopt;
        }
        emit_function(out, *function, layout, error);
        if (!error.empty()) {
            return std::nullopt;
        }
    }
    if (!module_.globals().empty()) {
        error = "KR16 globals are not implemented";
        return std::nullopt;
    }
    return out;
}

}  // namespace krc::backend

