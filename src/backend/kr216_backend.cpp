#include "backend/kr216_backend.h"

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

std::uint32_t type_alignment(const ir::TypePtr& type) {
    if (!type) return 1;
    switch (type->kind) {
    case ir::TypeKind::Integer:
        return type->bits <= 8 ? 1 : 2;
    case ir::TypeKind::Pointer:
        return 2;
    case ir::TypeKind::Array:
        return type_alignment(type->element);
    case ir::TypeKind::Struct:
    case ir::TypeKind::Union: {
        std::uint32_t alignment = 1;
        for (const auto& field : type->fields) {
            alignment = std::max(alignment, type_alignment(field));
        }
        return alignment;
    }
    default:
        return 2;
    }
}

std::uint32_t round_up(std::uint32_t value, std::uint32_t alignment) {
    return alignment == 0 ? value : (value + alignment - 1) / alignment * alignment;
}

std::uint32_t type_size(const ir::TypePtr& type) {
    if (!type) return 0;
    switch (type->kind) {
    case ir::TypeKind::Void:
        return 0;
    case ir::TypeKind::Integer:
        return static_cast<std::uint32_t>(std::max<std::uint64_t>(1, type->bits / 8));
    case ir::TypeKind::Pointer:
        return 2;
    case ir::TypeKind::Array:
        return type_size(type->element) * static_cast<std::uint32_t>(type->count);
    case ir::TypeKind::Struct: {
        std::uint32_t size = 0;
        std::uint32_t alignment = 1;
        for (const auto& field : type->fields) {
            const auto field_alignment = type_alignment(field);
            alignment = std::max(alignment, field_alignment);
            size = round_up(size, field_alignment);
            size += type_size(field);
        }
        return round_up(size, alignment);
    }
    case ir::TypeKind::Union: {
        std::uint32_t size = 0;
        for (const auto& field : type->fields) {
            size = std::max(size, type_size(field));
        }
        return size;
    }
    default:
        return 2;
    }
}

bool is_aggregate(const ir::TypePtr& type) {
    return type && (type->kind == ir::TypeKind::Struct ||
                    type->kind == ir::TypeKind::Union);
}

bool is_wide(const ir::TypePtr& type) {
    return type && type->kind == ir::TypeKind::Integer && type->bits > 16;
}

std::uint32_t word_count(const ir::TypePtr& type) {
    if (is_aggregate(type)) return 1;  // a hidden pointer
    return (type_size(type) + 1) / 2;
}

std::string debug_type_name(const ir::TypePtr& type) {
    auto name = ir::type_to_string(type);
    name.erase(std::remove(name.begin(), name.end(), '%'), name.end());
    std::replace(name.begin(), name.end(), ' ', '_');
    return name;
}

bool returns_aggregate(const ir::Function& function) {
    return function.type && !function.type->variadic &&
           is_aggregate(function.type->return_type);
}

bool gep_offset(const ir::Instruction& instruction, std::uint32_t& offset,
                std::string& error) {
    offset = 0;
    ir::TypePtr current = instruction.source_type;
    for (std::size_t index = 1; index < instruction.operands.size(); ++index) {
        const auto* constant =
            dynamic_cast<const ir::ConstantInt*>(instruction.operands[index]);
        if (!constant) {
            error = "kr216 dynamic GEP indices are not implemented";
            return false;
        }
        const auto element = constant->value;
        if (index == 1) {
            offset += static_cast<std::uint32_t>(element *
                                                 static_cast<std::int64_t>(type_size(current)));
            continue;
        }
        if (current && current->kind == ir::TypeKind::Array) {
            offset += static_cast<std::uint32_t>(
                element * static_cast<std::int64_t>(type_size(current->element)));
            current = current->element;
            continue;
        }
        if (current && current->kind == ir::TypeKind::Struct) {
            if (element < 0 || static_cast<std::size_t>(element) >= current->fields.size()) {
                error = "kr216 GEP struct index is out of range";
                return false;
            }
            for (std::int64_t field = 0; field < element; ++field) {
                offset += type_size(current->fields[static_cast<std::size_t>(field)]);
            }
            current = current->fields[static_cast<std::size_t>(element)];
            continue;
        }
        error = "kr216 GEP index does not select an aggregate element";
        return false;
    }
    return true;
}

struct ArgClass {
    const ir::Value* value = nullptr;
    std::uint32_t words = 1;
    std::uint32_t first_register = 0;
    std::uint32_t first_stack_word = 0;
    bool on_stack = false;
    bool aggregate = false;
};

std::vector<ArgClass> classify_arguments(const std::vector<const ir::Value*>& values,
                                         bool variadic) {
    std::vector<ArgClass> result;
    result.reserve(values.size());
    std::uint32_t registers = 0;
    std::uint32_t stack_words = 0;
    bool spilled = variadic;
    for (const auto* value : values) {
        ArgClass argument;
        argument.value = value;
        argument.aggregate = value && is_aggregate(value->type);
        argument.words = value ? word_count(value->type) : 1;
        if (!spilled && registers + argument.words <= 4) {
            argument.first_register = registers;
            registers += argument.words;
        } else {
            spilled = true;
            argument.on_stack = true;
            argument.first_stack_word = stack_words;
            stack_words += argument.words;
        }
        result.push_back(argument);
    }
    return result;
}

}  // namespace

Kr216Backend::Kr216Backend(const ir::Module& module, const target::TargetProfile& profile,
                           OptimizationLevel level)
    : module_(module), profile_(profile), level_(level) {}

Kr216Backend::FrameLayout Kr216Backend::layout_function(const ir::Function& function,
                                                        std::string& error) const {
    FrameLayout layout;
    std::uint32_t offset = 0;
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block->instructions) {
            if (instruction->opcode == ir::Opcode::Alloca) {
                const auto size = std::max<std::uint32_t>(
                    kSlotSize, type_size(instruction->allocated_type));
                offset = round_up(offset, 2);
                layout.allocas[instruction.get()] = offset;
                layout.slots[instruction.get()] = offset;
                offset += size;
                continue;
            }
            if (instruction->type && !ir::is_void(instruction->type)) {
                offset = round_up(offset, 2);
                layout.slots[instruction.get()] = offset;
                offset += std::max<std::uint32_t>(kSlotSize, type_size(instruction->type));
            }
        }
    }
    if (returns_aggregate(function)) {
        offset = round_up(offset, 2);
        layout.return_pointer = offset;
        layout.has_return_pointer = true;
        offset += kSlotSize;
    }
    for (const auto& parameter : function.parameters) {
        offset = round_up(offset, 2);
        layout.slots[parameter.get()] = offset;
        offset += std::max<std::uint32_t>(kSlotSize, type_size(parameter->type));
    }
    layout.frame_size = round_up(offset, 2);
    if (layout.frame_size > 0xFFFE) {
        error = "kr216 frame exceeds the ENTER range";
    }
    return layout;
}

std::uint32_t Kr216Backend::frame_bytes(const ir::Function& function) const {
    std::string error;
    return layout_function(function, error).frame_size;
}

void Kr216Backend::add_debug_type(ir::TypePtr type) const {
    if (!type || ir::is_void(type)) return;
    const auto name = debug_type_name(type);
    if (!debug_types_.insert(name).second) return;
    const auto size = type_size(type);
    debug_info_ += "T " + name + " " + std::to_string(size) + std::string(1, '\0');
    if (type->kind == ir::TypeKind::Struct) {
        debug_info_ += "C " + name + " " + std::to_string(size) + " -" +
                       std::string(1, '\0');
        std::uint32_t offset = 0;
        for (std::size_t index = 0; index < type->fields.size(); ++index) {
            const auto& field = type->fields[index];
            offset = round_up(offset, type_alignment(field));
            const auto field_size = type_size(field);
            const auto field_name = index < type->field_names.size()
                                        ? type->field_names[index]
                                        : std::to_string(index);
            debug_info_ += "M " + name + " " + field_name + " " +
                           std::to_string(offset) + " " + debug_type_name(field) +
                           " " + std::to_string(field_size) + std::string(1, '\0');
            add_debug_type(field);
            offset += field_size;
        }
    } else if (type->kind == ir::TypeKind::Array) {
        add_debug_type(type->element);
        debug_info_ += "A " + name + " " + debug_type_name(type->element) + " " +
                       std::to_string(type->count) + std::string(1, '\0');
    } else if (type->kind == ir::TypeKind::Pointer) {
        add_debug_type(type->element);
        debug_info_ += "R " + name + " " + debug_type_name(type->element) +
                       std::string(1, '\0');
    }
}

void Kr216Backend::emit_function(std::string& out, const ir::Function& function,
                                 const FrameLayout& layout, std::string& error) const {
    const auto slot = [&](const ir::Value* value) -> std::uint32_t {
        const auto found = layout.slots.find(value);
        if (found == layout.slots.end()) {
            error = "kr216 value '" + value->name + "' has no frame slot";
            return 0;
        }
        return found->second;
    };
    const auto add_immediate = [&](std::string_view reg, std::int32_t value) {
        if (value == 0) return;
        if (value >= -128 && value <= 127) {
            out += "    ADD " + std::string(reg) + ", #" + std::to_string(value) + "\n";
        } else {
            const std::string scratch = reg == "R7" ? "R6" : "R7";
            out += "    LD " + scratch + ", #" +
                   std::to_string(static_cast<std::uint16_t>(value)) + "\n";
            out += "    ADD " + std::string(reg) + ", " + scratch + "\n";
        }
    };
    // A value is loaded low word first. `adjust` compensates for bytes pushed
    // below SP after the frame was laid out.
    const auto frame_store = [&](std::uint32_t offset, std::string_view reg) {
        if (offset <= 127) {
            out += "    ST [SP+" + std::to_string(offset) + "], " + std::string(reg) +
                   "\n";
        } else {
            const std::string scratch = reg == "R6" ? "R7" : "R6";
            out += "    MOV " + scratch + ", SP\n";
            out += "    LD R5, #" + std::to_string(offset) + "\n";
            out += "    ADD " + scratch + ", R5\n";
            out += "    ST [" + scratch + "], " + std::string(reg) + "\n";
        }
    };
    const auto load_address = [&](const ir::Value* value, std::string_view reg,
                                  std::uint32_t adjust = 0) {
        const auto offset = slot(value) + adjust;
        out += "    MOV " + std::string(reg) + ", SP\n";
        if (offset <= 127) {
            add_immediate(reg, static_cast<std::int32_t>(offset));
        } else {
            const std::string scratch = reg == "R7" ? "R6" : "R7";
            out += "    LD " + scratch + ", #" + std::to_string(offset) + "\n";
            out += "    ADD " + std::string(reg) + ", " + scratch + "\n";
        }
    };
    const auto load_word = [&](const ir::Value* value, std::string_view reg,
                               bool high = false, std::uint32_t adjust = 0) {
        if (!value) {
            out += "    LD " + std::string(reg) + ", #0\n";
            return;
        }
        if (value->kind == ir::ValueKind::ConstantInt) {
            const auto constant = static_cast<const ir::ConstantInt*>(value)->value;
            const auto word = static_cast<std::uint16_t>(
                high ? ((constant >> 16) & 0xFFFF) : (constant & 0xFFFF));
            out += "    LD " + std::string(reg) + ", #" + std::to_string(word) + "\n";
            return;
        }
        if (value->kind == ir::ValueKind::ConstantNull ||
            value->kind == ir::ValueKind::ConstantUndef) {
            out += "    LD " + std::string(reg) + ", #0\n";
            return;
        }
        if (value->kind == ir::ValueKind::Global) {
            out += "    LD " + std::string(reg) + ", #" + value->name + "\n";
            return;
        }
        if (value->kind == ir::ValueKind::Instruction) {
            const auto* instruction = static_cast<const ir::Instruction*>(value);
            if (instruction->opcode == ir::Opcode::Alloca) {
                const auto offset = slot(value);
                out += "    MOV " + std::string(reg) + ", SP\n";
                if (offset + adjust <= 127) {
                    add_immediate(reg, static_cast<std::int32_t>(offset + adjust));
                } else {
                    const std::string scratch = reg == "R7" ? "R6" : "R7";
                    out += "    LD " + scratch + ", #" +
                           std::to_string(static_cast<std::uint16_t>(offset + adjust)) + "\n";
                    out += "    ADD " + std::string(reg) + ", " + scratch + "\n";
                }
                return;
            }
        }
        const auto offset = slot(value) + (high ? 2u : 0u) + adjust;
        if (offset <= 127) {
            out += "    LD " + std::string(reg) + ", [SP+" + std::to_string(offset) +
                   "]\n";
        } else {
            const std::string scratch = reg == "R7" ? "R6" : "R7";
            out += "    MOV " + scratch + ", SP\n";
            add_immediate(scratch, static_cast<std::int32_t>(offset));
            out += "    LD " + std::string(reg) + ", [" + scratch + "]\n";
        }
    };
    const auto store_word = [&](const ir::Instruction& instruction, std::string_view reg,
                                bool high = false, std::uint32_t adjust = 0) {
        const auto offset = slot(&instruction) + (high ? 2u : 0u) + adjust;
        if (offset <= 127) {
            out += "    ST [SP+" + std::to_string(offset) + "], " + std::string(reg) +
                   "\n";
        } else {
            out += "    MOV R7, SP\n";
            add_immediate("R7", static_cast<std::int32_t>(offset));
            out += "    ST [R7], " + std::string(reg) + "\n";
        }
    };
    const auto copy_from_pointer = [&](std::string_view pointer, std::uint32_t destination,
                                       std::uint32_t size) {
        // R0 and R1 are the architectural constants, not scratch registers; using
        // them here discarded every copied word.
        for (std::uint32_t offset = 0; offset < size; offset += kSlotSize) {
            out += "    LD R6, [" + std::string(pointer) + "+" + std::to_string(offset) + "]\n";
            frame_store(destination + offset, "R6");
        }
    };

    if (function.linkage == ir::Linkage::External ||
        function.linkage == ir::Linkage::Weak) {
        out += ".global " + function.name + "\n";
    }
    out += function.name + ":\n";
    out += "    ENTER #" + std::to_string(layout.frame_size) + ", #0\n";
    if (layout.has_return_pointer) {
        out += "    ST [SP+" + std::to_string(layout.return_pointer) + "], R6\n";
    }

    std::vector<const ir::Value*> parameter_values;
    parameter_values.reserve(function.parameters.size());
    for (const auto& parameter : function.parameters) parameter_values.push_back(parameter.get());
    const bool variadic = function.type && function.type->variadic;
    const auto parameters = classify_arguments(parameter_values, variadic);
    for (const auto& argument : parameters) {
        const auto destination = slot(argument.value);
        if (argument.on_stack) {
            const auto base = 4u + 2u * argument.first_stack_word;
            out += "    LD R7, [FP+" + std::to_string(base) + "]\n";
            if (argument.aggregate) {
                copy_from_pointer("R7", destination, type_size(argument.value->type));
            } else {
                frame_store(destination, "R7");
                if (argument.words == 2) {
                    out += "    LD R7, [FP+" + std::to_string(base + 2) + "]\n";
                    frame_store(destination + 2, "R7");
                }
            }
        } else {
            const auto first = static_cast<std::uint32_t>(2 + argument.first_register);
            if (argument.aggregate) {
                out += "    MOV R7, R" + std::to_string(first) + "\n";
                copy_from_pointer("R7", destination, type_size(argument.value->type));
            } else {
                frame_store(destination, "R" + std::to_string(first));
                if (argument.words == 2) {
                    frame_store(destination + 2, "R" + std::to_string(first + 1));
                }
            }
        }
    }

    std::size_t synthetic_label = 0;
    std::size_t debug_label_counter = 0;
    for (const auto& block : function.blocks) {
        out += label_for(*block) + ":\n";
        for (const auto& instruction : block->instructions) {
            if (instruction->opcode != ir::Opcode::Alloca && instruction->debug.valid()) {
                auto source = debug_sources_.find(instruction->debug.file);
                if (source == debug_sources_.end()) {
                    const auto id = next_debug_source_++;
                    source = debug_sources_.emplace(instruction->debug.file, id).first;
                    debug_info_ += "S " + std::to_string(id) + " " +
                                   instruction->debug.file + std::string(1, '\0');
                }
                const auto label = ".Ldbg." + function.name + "." +
                                   std::to_string(debug_label_counter++);
                out += label + ":\n";
                debug_info_ += "L " + label + " " +
                               std::to_string(source->second) + " " +
                               std::to_string(instruction->debug.line) + " " +
                               std::to_string(instruction->debug.column) +
                               std::string(1, '\0');
            }
            switch (instruction->opcode) {
            case ir::Opcode::Alloca:
                break;
            case ir::Opcode::VaStart: {
                // A variadic call passes every argument on the stack; the first
                // variable one follows the named parameters at FP+4.
                const auto fixed = function.parameters.size();
                out += "    MOV R2, FP\n";
                add_immediate("R2", static_cast<std::int32_t>(4 + 2 * fixed));
                store_word(*instruction, "R2");
                break;
            }
            case ir::Opcode::Load: {
                const auto size = type_size(instruction->type);
                if (is_aggregate(instruction->type)) {
                    load_word(instruction->operands[0], "R7");
                    copy_from_pointer("R7", slot(instruction.get()), size);
                    break;
                }
                load_word(instruction->operands[0], "R3");
                if (size == 1) {
                    out += (instruction->type && instruction->type->is_signed
                                ? "    LDBS R2, [R3]\n"
                                : "    LDBU R2, [R3]\n");
                    store_word(*instruction, "R2");
                } else {
                    out += "    LD R2, [R3]\n";
                    store_word(*instruction, "R2");
                    if (is_wide(instruction->type)) {
                        out += "    LD R2, [R3+2]\n";
                        store_word(*instruction, "R2", true);
                    }
                }
                break;
            }
            case ir::Opcode::Store: {
                const auto size = type_size(
                    instruction->operands[0] ? instruction->operands[0]->type : nullptr);
                load_word(instruction->operands[1], "R3");
                if (is_aggregate(instruction->operands[0] ? instruction->operands[0]->type
                                                          : nullptr)) {
                    const auto source = slot(instruction->operands[0]);
                    for (std::uint32_t offset = 0; offset < size; offset += kSlotSize) {
                        out += "    LD R6, [SP+" + std::to_string(source + offset) + "]\n";
                        out += "    ST [R3+" + std::to_string(offset) + "], R6\n";
                    }
                    break;
                }
                load_word(instruction->operands[0], "R2");
                if (size == 1) {
                    out += "    STB [R3], R2\n";
                } else {
                    out += "    ST [R3], R2\n";
                    if (size > 2) {
                        load_word(instruction->operands[0], "R2", true);
                        out += "    ST [R3+2], R2\n";
                    }
                }
                break;
            }
            case ir::Opcode::Add:
            case ir::Opcode::Sub:
            case ir::Opcode::And:
            case ir::Opcode::Or:
            case ir::Opcode::Xor: {
                const bool wide = is_wide(instruction->type);
                const auto mnemonic = instruction->opcode == ir::Opcode::Add
                                          ? "ADD"
                                          : instruction->opcode == ir::Opcode::Sub
                                                ? "SUB"
                                                : instruction->opcode == ir::Opcode::And
                                                      ? "AND"
                                                      : instruction->opcode == ir::Opcode::Or
                                                            ? "OR"
                                                            : "XOR";
                load_word(instruction->operands[0], "R2");
                load_word(instruction->operands[1], "R3");
                out += "    " + std::string(mnemonic) + " R2, R3\n";
                store_word(*instruction, "R2");
                if (wide) {
                    const auto carry = instruction->opcode == ir::Opcode::Add
                                           ? "ADDC"
                                           : instruction->opcode == ir::Opcode::Sub
                                                 ? "SUBB"
                                                 : mnemonic;
                    load_word(instruction->operands[0], "R2", true);
                    load_word(instruction->operands[1], "R3", true);
                    out += "    " + std::string(carry) + " R2, R3\n";
                    store_word(*instruction, "R2", true);
                }
                break;
            }
            case ir::Opcode::Mul:
            case ir::Opcode::SDiv:
            case ir::Opcode::UDiv:
            case ir::Opcode::SRem:
            case ir::Opcode::URem: {
                if (is_wide(instruction->type)) {
                    error = "kr216 32-bit multiply and division are not implemented";
                    return;
                }
                load_word(instruction->operands[0], "R2");
                load_word(instruction->operands[1], "R3");
                if (instruction->opcode == ir::Opcode::Mul) {
                    out += "    MUL R2, R3\n";
                    store_word(*instruction, "R2");
                } else if (instruction->opcode == ir::Opcode::SDiv ||
                           instruction->opcode == ir::Opcode::UDiv) {
                    out += instruction->opcode == ir::Opcode::SDiv ? "    DIVS R2, R3\n"
                                                                    : "    DIV R2, R3\n";
                    store_word(*instruction, "R2");
                } else {
                    out += "    MOV R4, R2\n";
                    out += instruction->opcode == ir::Opcode::SRem ? "    DIVS R2, R3\n"
                                                                    : "    DIV R2, R3\n";
                    out += "    MUL R2, R3\n";
                    out += "    SUB R4, R2\n";
                    store_word(*instruction, "R4");
                }
                break;
            }
            case ir::Opcode::Shl:
            case ir::Opcode::LShr:
            case ir::Opcode::AShr: {
                if (is_wide(instruction->type)) {
                    const auto* literal = [&]() -> const ir::ConstantInt* {
                        const auto* value = instruction->operands[1];
                        if (value && value->kind == ir::ValueKind::ConstantInt) {
                            return static_cast<const ir::ConstantInt*>(value);
                        }
                        if (value && value->kind == ir::ValueKind::Instruction) {
                            const auto* cast = static_cast<const ir::Instruction*>(value);
                            if (!cast->operands.empty() && cast->operands[0] &&
                                cast->operands[0]->kind == ir::ValueKind::ConstantInt) {
                                return static_cast<const ir::ConstantInt*>(cast->operands[0]);
                            }
                        }
                        return nullptr;
                    }();
                    if (!literal || literal->value < 0 || literal->value > 31) {
                        error = "kr216 dynamic 32-bit shifts are not implemented";
                        return;
                    }
                    const auto count = static_cast<int>(literal->value);
                    load_word(instruction->operands[0], "R2");
                    load_word(instruction->operands[0], "R3", true);
                    if (count == 0) {
                        store_word(*instruction, "R2");
                        store_word(*instruction, "R3", true);
                        break;
                    }
                    if (instruction->opcode == ir::Opcode::Shl) {
                        if (count < 16) {
                            out += "    MOV R4, R2\n";
                            out += "    SHL R2, #" + std::to_string(count) + "\n";
                            out += "    SHL R3, #" + std::to_string(count) + "\n";
                            out += "    SHR R4, #" + std::to_string(16 - count) + "\n";
                            out += "    OR R3, R4\n";
                        } else if (count == 16) {
                            out += "    MOV R3, R2\n";
                            out += "    LD R2, #0\n";
                        } else {
                            out += "    SHL R2, #" + std::to_string(count - 16) + "\n";
                            out += "    MOV R3, R2\n";
                            out += "    LD R2, #0\n";
                        }
                    } else if (instruction->opcode == ir::Opcode::LShr) {
                        if (count < 16) {
                            out += "    MOV R4, R2\n";
                            out += "    SHR R2, #" + std::to_string(count) + "\n";
                            out += "    SHL R4, #" + std::to_string(16 - count) + "\n";
                            out += "    OR R2, R4\n";
                            out += "    SHR R3, #" + std::to_string(count) + "\n";
                        } else if (count == 16) {
                            out += "    MOV R2, R3\n";
                            out += "    LD R3, #0\n";
                        } else {
                            out += "    MOV R2, R3\n";
                            out += "    SHR R2, #" + std::to_string(count - 16) + "\n";
                            out += "    LD R3, #0\n";
                        }
                    } else {
                        if (count < 16) {
                            out += "    MOV R4, R2\n";
                            out += "    SHR R2, #" + std::to_string(count) + "\n";
                            out += "    SHL R4, #" + std::to_string(16 - count) + "\n";
                            out += "    OR R2, R4\n";
                            out += "    SAR R3, #" + std::to_string(count) + "\n";
                        } else if (count == 16) {
                            out += "    MOV R2, R3\n";
                            out += "    SAR R3, #15\n";
                        } else {
                            out += "    MOV R2, R3\n";
                            out += "    SAR R2, #" + std::to_string(count - 16) + "\n";
                            out += "    SAR R3, #15\n";
                        }
                    }
                    store_word(*instruction, "R2");
                    store_word(*instruction, "R3", true);
                    break;
                }
                const auto mnemonic = instruction->opcode == ir::Opcode::Shl
                                          ? "SHL"
                                          : instruction->opcode == ir::Opcode::LShr ? "SHR"
                                                                                     : "SAR";
                load_word(instruction->operands[0], "R2");
                if (const auto* literal =
                        dynamic_cast<const ir::ConstantInt*>(instruction->operands[1]);
                    literal && literal->value >= 0 && literal->value <= 15) {
                    out += "    " + std::string(mnemonic) + " R2, #" +
                           std::to_string(literal->value) + "\n";
                } else {
                    load_word(instruction->operands[1], "R3");
                    out += "    " + std::string(mnemonic) + " R2, R3\n";
                }
                store_word(*instruction, "R2");
                break;
            }
            case ir::Opcode::ICmp: {
                if (is_wide(instruction->operands[0]->type)) {
                    // Equality is enough for the M3 literal check; the ordered
                    // predicates need the high/low split and are refused loudly.
                    if (instruction->icmp_predicate != ir::ICmpPredicate::Eq &&
                        instruction->icmp_predicate != ir::ICmpPredicate::Ne) {
                        error = "kr216 32-bit ordered comparison is not implemented";
                        return;
                    }
                    const bool equality =
                        instruction->icmp_predicate == ir::ICmpPredicate::Eq;
                    const auto branch_label = ".Lcmp.branch." + function.name + "." +
                                              std::to_string(synthetic_label);
                    const auto done_label = ".Lcmp.done." + function.name + "." +
                                            std::to_string(synthetic_label);
                    ++synthetic_label;
                    load_word(instruction->operands[0], "R2", true);
                    load_word(instruction->operands[1], "R3", true);
                    out += "    CMP R2, R3\n";
                    out += "    BNE " + branch_label + "\n";
                    load_word(instruction->operands[0], "R2");
                    load_word(instruction->operands[1], "R3");
                    out += "    CMP R2, R3\n";
                    out += "    BNE " + branch_label + "\n";
                    out += std::string("    LD R2, #") + (equality ? "1\n" : "0\n");
                    out += "    JMP " + done_label + "\n";
                    out += branch_label + ":\n";
                    out += std::string("    LD R2, #") + (equality ? "0\n" : "1\n");
                    out += done_label + ":\n";
                    store_word(*instruction, "R2");
                    break;
                }
                load_word(instruction->operands[0], "R2");
                load_word(instruction->operands[1], "R3");
                out += "    CMP R2, R3\n";
                const auto true_label = ".Lcmp.true." + function.name + "." +
                                        std::to_string(synthetic_label);
                const auto end_label = ".Lcmp.end." + function.name + "." +
                                       std::to_string(synthetic_label);
                ++synthetic_label;
                std::string condition = "BEQ";
                switch (instruction->icmp_predicate) {
                case ir::ICmpPredicate::Eq: condition = "BEQ"; break;
                case ir::ICmpPredicate::Ne: condition = "BNE"; break;
                case ir::ICmpPredicate::Ult: condition = "BCC"; break;
                case ir::ICmpPredicate::Ule: condition = "BLS"; break;
                case ir::ICmpPredicate::Ugt: condition = "BHI"; break;
                case ir::ICmpPredicate::Uge: condition = "BCS"; break;
                case ir::ICmpPredicate::Slt: condition = "BLT"; break;
                case ir::ICmpPredicate::Sle: condition = "BLE"; break;
                case ir::ICmpPredicate::Sgt: condition = "BGT"; break;
                case ir::ICmpPredicate::Sge: condition = "BGE"; break;
                }
                out += "    " + condition + " " + true_label + "\n";
                out += "    LD R2, #0\n";
                out += "    JMP " + end_label + "\n";
                out += true_label + ":\n";
                out += "    LD R2, #1\n";
                out += end_label + ":\n";
                store_word(*instruction, "R2");
                break;
            }
            case ir::Opcode::BitCast:
            case ir::Opcode::PtrToInt:
            case ir::Opcode::IntToPtr:
            case ir::Opcode::ZExt:
            case ir::Opcode::SExt:
            case ir::Opcode::Trunc: {
                load_word(instruction->operands[0], "R2");
                store_word(*instruction, "R2");
                if (is_wide(instruction->type)) {
                    if (instruction->opcode == ir::Opcode::SExt) {
                        out += "    MOV R3, R2\n";
                        out += "    SAR R3, #15\n";
                    } else if (instruction->opcode == ir::Opcode::ZExt) {
                        out += "    LD R3, #0\n";
                    } else {
                        load_word(instruction->operands[0], "R3", true);
                    }
                    store_word(*instruction, "R3", true);
                }
                break;
            }
            case ir::Opcode::GetElementPtr: {
                const bool dynamic_index =
                    instruction->operands.size() > 1 &&
                    dynamic_cast<const ir::ConstantInt*>(instruction->operands[1]) == nullptr;
                std::uint32_t offset = 0;
                if (!dynamic_index) {
                    if (!gep_offset(*instruction, offset, error)) return;
                } else if (instruction->operands.size() != 2) {
                    error = "kr216 dynamic GEP with more than one index is not implemented";
                    return;
                }
                load_word(instruction->operands[0], "R2");
                if (dynamic_index) {
                    load_word(instruction->operands[1], "R3");
                    const auto size = type_size(instruction->source_type);
                    if (size == 2) {
                        out += "    ADD R3, R3\n";
                    } else if (size == 4) {
                        out += "    SHL R3, #2\n";
                    } else if (size == 8) {
                        out += "    SHL R3, #3\n";
                    } else if (size != 1) {
                        out += "    LD R4, #" + std::to_string(size) + "\n";
                        out += "    MUL R3, R4\n";
                    }
                    out += "    ADD R2, R3\n";
                }
                if (offset != 0) add_immediate("R2", static_cast<std::int32_t>(offset));
                store_word(*instruction, "R2");
                break;
            }
            case ir::Opcode::Br:
                if (instruction->successors.empty()) {
                    error = "kr216 branch has no destination";
                    return;
                }
                out += "    JMP " + label_for(*instruction->successors[0]) + "\n";
                break;
            case ir::Opcode::CondBr:
                if (instruction->successors.size() < 2) {
                    error = "kr216 conditional branch needs two destinations";
                    return;
                }
                load_word(instruction->operands[0], "R2");
                out += "    CMP R2, #0\n";
                {
                    const auto false_label = ".Lcond.false." + function.name + "." +
                                             std::to_string(synthetic_label++);
                    out += "    BEQ " + false_label + "\n";
                    out += "    JMP " + label_for(*instruction->successors[0]) + "\n";
                    out += false_label + ":\n";
                    out += "    JMP " + label_for(*instruction->successors[1]) + "\n";
                }
                break;
            case ir::Opcode::Call: {
                const auto* callee = instruction->operands[0];
                std::vector<const ir::Value*> values;
                for (std::size_t index = 1; index < instruction->operands.size(); ++index) {
                    values.push_back(instruction->operands[index]);
                }
                const auto* callee_type = [&]() -> const ir::Type* {
                    if (!callee || !callee->type) return nullptr;
                    if (callee->type->kind == ir::TypeKind::Function) return callee->type;
                    if (callee->type->kind == ir::TypeKind::Pointer && callee->type->element &&
                        callee->type->element->kind == ir::TypeKind::Function) {
                        return callee->type->element;
                    }
                    return nullptr;
                }();
                const bool variadic_call = callee_type && callee_type->variadic;
                const auto arguments = classify_arguments(values, variadic_call);
                for (const auto& argument : arguments) {
                    if (argument.on_stack) continue;
                    const auto first = static_cast<std::uint32_t>(2 + argument.first_register);
                    if (argument.aggregate) {
                        load_address(argument.value, "R" + std::to_string(first));
                    } else {
                        load_word(argument.value, "R" + std::to_string(first));
                        if (argument.words == 2) {
                            load_word(argument.value, "R" + std::to_string(first + 1), true);
                        }
                    }
                }
                std::uint32_t pushed_words = 0;
                for (std::size_t index = arguments.size(); index > 0; --index) {
                    const auto& argument = arguments[index - 1];
                    if (!argument.on_stack) continue;
                    // Stage through R7, not R2: the register arguments already
                    // occupy R2-R5 and loading a stack argument into R2 would
                    // destroy argument 0 before the call.
                    if (argument.aggregate) {
                        load_address(argument.value, "R7",
                                     static_cast<std::uint32_t>(2 * pushed_words));
                        out += "    PUSH R7\n";
                        ++pushed_words;
                        continue;
                    }
                    if (argument.words == 2) {
                        load_word(argument.value, "R7", true,
                                  static_cast<std::uint32_t>(2 * pushed_words));
                        out += "    PUSH R7\n";
                        ++pushed_words;
                    }
                    load_word(argument.value, "R7", false,
                              static_cast<std::uint32_t>(2 * pushed_words));
                    out += "    PUSH R7\n";
                    ++pushed_words;
                }
                const bool aggregate_return = is_aggregate(instruction->type);
                if (aggregate_return) {
                    const auto destination = slot(instruction.get()) + pushed_words * 2;
                    if (destination > 127) {
                        error = "kr216 aggregate destination offset is too large";
                        return;
                    }
                    out += "    MOV R6, SP\n";
                    add_immediate("R6", static_cast<std::int32_t>(destination));
                }
                if (callee && callee->kind == ir::ValueKind::Function) {
                    out += "    CALL " + callee->name + "\n";
                } else {
                    load_word(callee, "R7", false, pushed_words * 2);
                    out += "    CALL R7\n";
                }
                if (pushed_words > 0) add_immediate("SP", static_cast<std::int32_t>(pushed_words * 2));
                if (!aggregate_return && instruction->type && !ir::is_void(instruction->type)) {
                    store_word(*instruction, "R2");
                    if (is_wide(instruction->type)) {
                        store_word(*instruction, "R3", true);
                    }
                }
                break;
            }
            case ir::Opcode::Ret:
                if (!instruction->operands.empty() && instruction->operands[0]) {
                    const auto& value = instruction->operands[0];
                    if (is_aggregate(value->type)) {
                        if (!layout.has_return_pointer) {
                            error = "kr216 aggregate return has no destination";
                            return;
                        }
                        const auto size = type_size(value->type);
                        out += "    LD R7, [SP+" + std::to_string(layout.return_pointer) + "]\n";
                        const auto source = slot(value);
                        for (std::uint32_t offset = 0; offset < size; offset += kSlotSize) {
                            out += "    LD R6, [SP+" + std::to_string(source + offset) + "]\n";
                            out += "    ST [R7+" + std::to_string(offset) + "], R6\n";
                        }
                    } else {
                        load_word(value, "R2");
                        if (is_wide(value->type)) load_word(value, "R3", true);
                    }
                }
                out += "    JMP .L" + function.name + ".ret\n";
                break;
            case ir::Opcode::Unreachable:
                out += "    JMP .L" + function.name + ".ret\n";
                break;
            default:
                error = "kr216 backend does not support opcode " +
                        std::string(ir::opcode_name(instruction->opcode));
                return;
            }
        }
    }
    const auto& display_name =
        function.debug_name.empty() ? function.name : function.debug_name;
    debug_info_ += "F " + display_name + " " + function.name + " .L" +
                   function.name + ".ret " + std::to_string(layout.frame_size) + " " +
                   debug_type_name(function.type ? function.type->return_type : nullptr) +
                   std::string(1, '\0');
    debug_info_ += "K " + function.name + " " + function.name + " - " +
                   function.name + " .L" + function.name + ".ret" +
                   std::string(1, '\0');
    const auto frame_offset = [&](std::uint32_t slot_offset) {
        return layout.frame_size >= slot_offset ? layout.frame_size - slot_offset : 0u;
    };
    std::unordered_set<std::string> debug_variable_names;
    std::unordered_set<std::string> parameter_names;
    for (const auto& parameter : function.parameters) {
        parameter_names.insert(parameter->name);
        const auto found = layout.slots.find(parameter.get());
        if (found == layout.slots.end() || !parameter->type) continue;
        if (!debug_variable_names.insert(function.name + "." + parameter->name).second) {
            continue;
        }
        add_debug_type(parameter->type);
        debug_info_ += "V " + function.name + " " + parameter->name + " " +
                       std::to_string(frame_offset(found->second)) + " " +
                       debug_type_name(parameter->type) + " " +
                       std::to_string(type_size(parameter->type)) +
                       std::string(1, '\0');
        debug_info_ += "W " + function.name + " " + parameter->name + " param " +
                       function.name + " 0x0" + std::string(1, '\0');
    }
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block->instructions) {
            if (instruction->opcode != ir::Opcode::Alloca ||
                !instruction->allocated_type) {
                continue;
            }
            auto name = instruction->name;
            if (name.size() <= 5 || name.compare(name.size() - 5, 5, ".addr") != 0) {
                continue;
            }
            name.resize(name.size() - 5);
            if (parameter_names.contains(name)) continue;
            const auto found = layout.allocas.find(instruction.get());
            if (found == layout.allocas.end()) continue;
            if (!debug_variable_names.insert(function.name + "." + name).second) continue;
            add_debug_type(instruction->allocated_type);
            debug_info_ += "V " + function.name + " " + name + " " +
                           std::to_string(frame_offset(found->second)) + " " +
                           debug_type_name(instruction->allocated_type) + " " +
                           std::to_string(type_size(instruction->allocated_type)) +
                           std::string(1, '\0');
            debug_info_ += "W " + function.name + " " + name + " local " +
                           function.name + " 0x0" + std::string(1, '\0');
        }
    }

    out += ".L" + function.name + ".ret:\n";
    out += "    LEAVE #0\n";
    out += "    RET\n";
}

std::optional<std::string> Kr216Backend::emit(std::string& error) const {
    static_cast<void>(profile_);
    static_cast<void>(level_);
    std::string out = "; KR216 assembly generated by krc\n";
    debug_info_.clear();
    debug_sources_.clear();
    debug_types_.clear();
    next_debug_source_ = 0;
    out += ".text\n";
    for (const auto& function : module_.functions()) {
        if (function->declaration) {
            out += ".extern " + function->name + "\n";
            continue;
        }
        for (const auto& parameter : function->parameters) {
            if (!parameter->type) continue;
            if (ir::is_float(parameter->type) ||
                (parameter->type->kind == ir::TypeKind::Integer &&
                 parameter->type->bits > 32)) {
                error = "kr216 floating-point and 64-bit parameters are not implemented";
                return std::nullopt;
            }
        }
        const auto layout = layout_function(*function, error);
        if (!error.empty()) return std::nullopt;
        emit_function(out, *function, layout, error);
        if (!error.empty()) return std::nullopt;
    }
    for (const auto& global : module_.globals()) {
        if (global->external) {
            out += ".extern " + global->name + "\n";
            continue;
        }
        const auto size = type_size(global->element_type);
        const auto* data =
            dynamic_cast<const ir::ConstantDataArray*>(global->initializer);
        const auto* constant =
            dynamic_cast<const ir::ConstantInt*>(global->initializer);
        const bool zero_initialized =
            global->initializer == nullptr ||
            (constant != nullptr && constant->value == 0) ||
            (data != nullptr &&
             std::all_of(data->bytes.begin(), data->bytes.end(),
                         [](std::uint8_t byte) { return byte == 0; }));
        if (global->name.starts_with(".str.")) {
            out += ".rodata\n";
        } else {
            out += zero_initialized ? ".bss\n" : ".data\n";
        }
        if (!global->internal) out += ".global " + global->name + "\n";
        out += global->name + ":\n";
        if (zero_initialized) {
            out += "    .space " + std::to_string(size) + "\n";
            continue;
        }
        if (const auto* addresses =
                dynamic_cast<const ir::ConstantAddressArray*>(global->initializer)) {
            for (const auto* element : addresses->elements) {
                if (element && (element->kind == ir::ValueKind::Function ||
                                element->kind == ir::ValueKind::Global)) {
                    out += "    .word " + element->name + "\n";
                } else {
                    out += "    .word 0\n";
                }
            }
            continue;
        }
        if (data != nullptr) {
            out += "    .byte ";
            for (std::size_t index = 0; index < data->bytes.size(); ++index) {
                if (index != 0) out += ", ";
                out += std::to_string(data->bytes[index]);
            }
            out += "\n";
            if (data->bytes.size() % 2 != 0) out += "    .byte 0\n";
            continue;
        }
        if (constant != nullptr) {
            for (std::uint32_t offset = 0; offset < size; offset += 2) {
                out += "    .word " +
                       std::to_string(static_cast<std::uint16_t>(
                           (static_cast<std::uint64_t>(constant->value) >> (offset * 8)) &
                           0xFFFF)) +
                       "\n";
            }
            continue;
        }
        for (std::uint32_t offset = 0; offset < size; offset += 2) out += "    .word 0\n";
    }
    for (const auto& global : module_.globals()) {
        if (global->external) continue;
        add_debug_type(global->element_type);
        debug_info_ += "G " + global->name + " " + global->name + " " +
                       debug_type_name(global->element_type) + " " +
                       std::to_string(type_size(global->element_type)) +
                       std::string(1, '\0');
    }
    return out;
}

}  // namespace krc::backend
