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

std::string inverse_condition_for(ir::ICmpPredicate predicate) {
    switch (predicate) {
    case ir::ICmpPredicate::Eq:
        return "BNE";
    case ir::ICmpPredicate::Ne:
        return "BEQ";
    case ir::ICmpPredicate::Ult:
        return "BCS";
    case ir::ICmpPredicate::Ule:
        return "BHI";
    case ir::ICmpPredicate::Ugt:
        return "BLS";
    case ir::ICmpPredicate::Uge:
        return "BCC";
    case ir::ICmpPredicate::Slt:
        return "BGE";
    case ir::ICmpPredicate::Sle:
        return "BGT";
    case ir::ICmpPredicate::Sgt:
        return "BLE";
    case ir::ICmpPredicate::Sge:
        return "BLT";
    }
    return "BNE";
}

std::uint32_t type_size(const ir::TypePtr& type) {
    if (!type) {
        return 0;
    }
    switch (type->kind) {
    case ir::TypeKind::Void:
        return 0;
    case ir::TypeKind::Integer:
        return static_cast<std::uint32_t>(
            std::max<std::uint64_t>(1, type->bits / 8));
    case ir::TypeKind::Pointer:
        return 2;
    case ir::TypeKind::Array:
        return type_size(type->element) *
               static_cast<std::uint32_t>(type->count);
    case ir::TypeKind::Struct: {
        std::uint32_t size = 0;
        for (const auto& field : type->fields) {
            size = (size + 1) & ~1u;
            size += type_size(field);
        }
        return (size + 1) & ~1u;
    }
    default:
        return 2;
    }
}

bool gep_offset(const ir::Instruction& instruction, std::uint32_t& offset,
                std::string& error) {
    offset = 0;
    ir::TypePtr current = instruction.source_type;
    for (std::size_t index = 1; index < instruction.operands.size(); ++index) {
        const auto* constant =
            dynamic_cast<const ir::ConstantInt*>(instruction.operands[index]);
        if (!constant) {
            error = "KR16 dynamic GEP indices are not implemented yet";
            return false;
        }
        const auto element = constant->value;
        if (index == 1) {
            offset += static_cast<std::uint32_t>(
                element *
                static_cast<std::int64_t>(type_size(current)));
            continue;
        }
        if (current && current->kind == ir::TypeKind::Array) {
            offset += static_cast<std::uint32_t>(
                element *
                static_cast<std::int64_t>(type_size(current->element)));
            current = current->element;
            continue;
        }
        if (current && current->kind == ir::TypeKind::Struct) {
            if (element < 0 ||
                static_cast<std::size_t>(element) >= current->fields.size()) {
                error = "KR16 GEP struct index is out of range";
                return false;
            }
            for (std::int64_t field = 0; field < element; ++field) {
                offset += type_size(
                    current->fields[static_cast<std::size_t>(field)]);
            }
            current = current->fields[static_cast<std::size_t>(element)];
            continue;
        }
        error = "KR16 GEP index does not select an aggregate element";
        return false;
    }
    return true;
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
                layout.slots[instruction.get()] = offset;
                offset += std::max<std::uint32_t>(
                    kSlotSize, type_size(instruction->allocated_type));
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
    // `adjust` compensates for bytes pushed below SP: the staged slots sit
    // above SP, so their SP-relative offsets grow by everything pushed.
    const auto load = [&](const ir::Value* value, std::string_view reg,
                          std::uint32_t adjust = 0) {
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
        if (value->kind == ir::ValueKind::ConstantNull ||
            value->kind == ir::ValueKind::ConstantUndef) {
            out += "    MOV " + std::string(reg) + ", #0\n";
            return;
        }
        if (value->kind == ir::ValueKind::Global) {
            out += "    LEA " + std::string(reg) + ", [" + value->name + "]\n";
            return;
        }
        if (value->kind == ir::ValueKind::Instruction) {
            const auto* instruction =
                static_cast<const ir::Instruction*>(value);
            if (instruction->opcode == ir::Opcode::Alloca) {
                out += "    LEA " + std::string(reg) + ", [SP+" +
                       std::to_string(slot(value) + adjust) + "]\n";
                return;
            }
        }
        out += "    LD " + std::string(reg) + ", [SP+" +
               std::to_string(slot(value) + adjust) + "]\n";
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
    // A variadic callee receives every argument on the stack, so one rule reads
    // both the named parameters and the variable ones (ADR 0029).
    const bool variadic = function.type && function.type->variadic;
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        if (variadic) {
            out += "    LD R0, [R4+" + std::to_string(4 + 2 * index) + "]\n";
            out += "    ST [SP+" +
                   std::to_string(slot(function.parameters[index].get())) +
                   "], R0\n";
            continue;
        }
        if (index < 3) {
            out += "    ST [SP+" +
                   std::to_string(slot(function.parameters[index].get())) + "], " +
                   arguments[index] + "\n";
            continue;
        }
        // The caller pushed the extras above the return address, last argument
        // first, so the fourth sits just past the saved frame pointer
        // (docs/abi-kr16.md, ADR 0028).
        out += "    LD R0, [R4+" + std::to_string(4 + 2 * (index - 3)) + "]\n";
        out += "    ST [SP+" +
               std::to_string(slot(function.parameters[index].get())) + "], R0\n";
    }

    std::size_t synthetic_label = 0;
    for (const auto& block : function.blocks) {
        out += label_for(*block) + ":\n";
        for (const auto& instruction : block->instructions) {
            switch (instruction->opcode) {
            case ir::Opcode::Alloca:
                break;
            case ir::Opcode::VaStart: {
                // The first variable argument follows the named parameters in
                // the caller's pushed area (ADR 0029).
                const auto fixed = function.parameters.size();
                out += "    LEA R0, [R4+" + std::to_string(4 + 2 * fixed) + "]\n";
                store(*instruction, "R0");
                break;
            }
            case ir::Opcode::Load:
                load(instruction->operands[0], "R0");
                out += instruction->type && instruction->type->bits == 8
                           ? "    LDB R1, [R0]\n"
                           : "    LD R1, [R0]\n";
                store(*instruction, "R1");
                break;
            case ir::Opcode::Store:
                load(instruction->operands[0], "R0");
                load(instruction->operands[1], "R1");
                out += instruction->operands[0] &&
                               instruction->operands[0]->type &&
                               instruction->operands[0]->type->bits == 8
                           ? "    STB [R1], R0\n"
                           : "    ST [R1], R0\n";
                break;
            case ir::Opcode::Mul:
            case ir::Opcode::SDiv:
            case ir::Opcode::UDiv:
            case ir::Opcode::SRem:
            case ir::Opcode::URem:
                load(instruction->operands[0], "R0");
                load(instruction->operands[1], "R1");
                out += instruction->opcode == ir::Opcode::Mul
                           ? "    MUL R0, R1\n"
                           : (instruction->opcode == ir::Opcode::SDiv
                                  ? "    DIV R0, R1\n"
                                  : (instruction->opcode == ir::Opcode::UDiv
                                         ? "    DIVU R0, R1\n"
                                         : (instruction->opcode ==
                                                    ir::Opcode::SRem
                                                ? "    MOD R0, R1\n"
                                                : "    MODU R0, R1\n")));
                store(*instruction, "R0");
                break;
            case ir::Opcode::Shl:
            case ir::Opcode::LShr:
            case ir::Opcode::AShr:
                load(instruction->operands[0], "R0");
                load(instruction->operands[1], "R1");
                out += instruction->opcode == ir::Opcode::Shl
                           ? "    SHL R0, R1\n"
                           : (instruction->opcode == ir::Opcode::LShr
                                  ? "    SHR R0, R1\n"
                                  : "    SAR R0, R1\n");
                store(*instruction, "R0");
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
                const auto false_label =
                    ".Lcmp.false." + function.name + "." +
                    std::to_string(synthetic_label);
                const auto true_label =
                    ".Lcmp.true." + function.name + "." +
                    std::to_string(synthetic_label);
                const auto end_label =
                    ".Lcmp.end." + function.name + "." +
                    std::to_string(synthetic_label);
                ++synthetic_label;
                out += "    " +
                       inverse_condition_for(instruction->icmp_predicate) + " " +
                       false_label + "\n";
                out += "    JMP " + true_label + "\n";
                out += false_label + ":\n";
                out += "    MOV R0, #0\n";
                out += "    JMP " + end_label + "\n";
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
                    out += "    MOV R0, #" + instruction->operands[0]->name +
                           "\n";
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
                out += "    JMP " + label_for(*instruction->successors[0]) +
                       "\n";
                break;
            case ir::Opcode::CondBr: {
                if (instruction->successors.size() < 2) {
                    error = "KR16 conditional branch needs two destinations";
                    return;
                }
                load(instruction->operands[0], "R0");
                out += "    CMP R0, #0\n";
                const auto false_label =
                    ".Lcond.false." + function.name + "." +
                    std::to_string(synthetic_label);
                ++synthetic_label;
                out += "    BEQ " + false_label + "\n";
                out += "    JMP " + label_for(*instruction->successors[0]) +
                       "\n";
                out += false_label + ":\n";
                out += "    JMP " + label_for(*instruction->successors[1]) +
                       "\n";
                break;
            }
            case ir::Opcode::Call: {
                const auto argument_count = instruction->operands.size() - 1;
                const auto* callee_type = [&]() -> const ir::Type* {
                    const auto* callee = instruction->operands[0];
                    if (!callee || !callee->type) {
                        return nullptr;
                    }
                    if (callee->type->kind == ir::TypeKind::Function) {
                        return callee->type;
                    }
                    if (callee->type->kind == ir::TypeKind::Pointer &&
                        callee->type->element &&
                        callee->type->element->kind == ir::TypeKind::Function) {
                        return callee->type->element;
                    }
                    return nullptr;
                }();
                const bool variadic_call = callee_type && callee_type->variadic;
                // Register arguments first: the staged values are addressed
                // relative to SP, which pushing the extras moves.
                if (!variadic_call) {
                    for (std::size_t index = 0;
                         index < argument_count && index < 3; ++index) {
                        load(instruction->operands[index + 1], arguments[index]);
                    }
                }
                // Extras go out last first, so the first argument ends at the
                // lowest address and they run upward in declaration order. A
                // variadic call passes every argument this way.
                const std::size_t first_pushed = variadic_call ? 1 : 4;
                for (std::size_t index = argument_count, pushed = 0;
                     index >= first_pushed; --index, ++pushed) {
                    load(instruction->operands[index], "R0",
                         static_cast<std::uint32_t>(2 * pushed));
                    out += "    PUSH R0\n";
                    if (index == first_pushed) {
                        break;
                    }
                }
                const auto pushed_count =
                    argument_count >= first_pushed
                        ? argument_count - first_pushed + 1
                        : std::size_t{0};
                const auto* callee = instruction->operands[0];
                if (callee->kind == ir::ValueKind::Function) {
                    out += "    MOV R0, #" + callee->name + "\n";
                } else {
                    // The callee value is read after the pushes, so its slot
                    // offset carries the same adjustment as the arguments.
                    load(callee, "R0",
                         static_cast<std::uint32_t>(2 * pushed_count));
                }
                out += "    CALL R0\n";
                if (pushed_count > 0) {
                    out += "    LEA SP, [SP+" + std::to_string(2 * pushed_count) +
                           "]\n";
                }
                if (instruction->type && !ir::is_void(instruction->type)) {
                    store(*instruction, "R0");
                }
                break;
            }
            case ir::Opcode::GetElementPtr: {
                const bool dynamic_index =
                    instruction->operands.size() == 2 &&
                    dynamic_cast<const ir::ConstantInt*>(
                        instruction->operands[1]) == nullptr;
                std::uint32_t offset = 0;
                if (!dynamic_index &&
                    !gep_offset(*instruction, offset, error)) {
                    return;
                }
                if (!instruction->operands.empty() &&
                    instruction->operands[0] &&
                    instruction->operands[0]->kind ==
                        ir::ValueKind::Global) {
                    out += "    LEA R0, [" + instruction->operands[0]->name +
                           "]\n";
                } else {
                    load(instruction->operands[0], "R0");
                }
                if (dynamic_index) {
                    const auto size = type_size(instruction->source_type);
                    load(instruction->operands[1], "R1");
                    if (size == 2) {
                        out += "    ADD R1, R1\n";
                    } else if (size == 4) {
                        out += "    SHL R1, #2\n";
                    } else if (size == 8) {
                        out += "    SHL R1, #3\n";
                    } else if (size != 1) {
                        error = "KR16 dynamic GEP element size is unsupported";
                        return;
                    }
                    out += "    ADD R0, R1\n";
                } else if (offset != 0) {
                    out += "    MOV R1, #" + std::to_string(offset) + "\n";
                    out += "    ADD R0, R1\n";
                }
                store(*instruction, "R0");
                break;
            }
            case ir::Opcode::Ret:
                if (!instruction->operands.empty() &&
                    instruction->operands[0]) {
                    // An aggregate return has no register form on this
                    // profile; rejecting it beats storing a truncated value.
                    if (instruction->operands[0]->type &&
                        (instruction->operands[0]->type->kind ==
                             ir::TypeKind::Struct ||
                         instruction->operands[0]->type->kind ==
                             ir::TypeKind::Union)) {
                        error = "KR16 aggregate returns are not implemented";
                        return;
                    }
                    load(instruction->operands[0], "R0");
                }
                out += "    JMP .L" + function.name + ".ret\n";
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
    for (const auto& global : module_.globals()) {
        if (global->external) {
            out += "    .extern " + global->name + "\n";
            continue;
        }
        const auto* data =
            dynamic_cast<const ir::ConstantDataArray*>(global->initializer);
        if (global->name.empty() || global->name.front() != '.') {
            if (!global->internal) {
                out += "    .global " + global->name + "\n";
            }
        }
        out += global->name + ":\n";
        if (data) {
            out += "    .byte ";
            for (std::size_t index = 0; index < data->bytes.size(); ++index) {
                if (index != 0) {
                    out += ", ";
                }
                out += std::to_string(data->bytes[index]);
            }
            out += "\n";
            if (data->bytes.size() % 2 != 0) {
                out += "    .byte 0\n";
            }
            continue;
        }
        if (const auto* constant =
                dynamic_cast<const ir::ConstantInt*>(global->initializer)) {
            const auto size = type_size(global->element_type);
            out += "    .byte ";
            for (std::uint32_t index = 0; index < size; ++index) {
                if (index != 0) {
                    out += ", ";
                }
                out += std::to_string(
                    static_cast<std::uint8_t>(
                        (constant->value >> (index * 8)) & 0xFF));
            }
            out += "\n";
            continue;
        }
        const auto size = type_size(global->element_type);
        for (std::uint32_t offset = 0; offset < size; offset += 16) {
            out += "    .byte ";
            const auto count = std::min<std::uint32_t>(16, size - offset);
            for (std::uint32_t index = 0; index < count; ++index) {
                if (index != 0) {
                    out += ", ";
                }
                out += "0";
            }
            out += "\n";
        }
    }
    return out;
}

}  // namespace krc::backend
