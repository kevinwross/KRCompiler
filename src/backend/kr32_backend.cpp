#include "backend/kr32_backend.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "backend/peephole.h"

namespace krc::backend {

namespace {

std::uint64_t round_up(std::uint64_t value, std::uint64_t alignment) {
    if (alignment <= 1) {
        return value;
    }
    return (value + alignment - 1) / alignment * alignment;
}

std::string register_name(std::size_t index) {
    return "R" + std::to_string(index);
}

std::string load_opcode(ir::TypePtr type) {
    if (!type || type->kind != ir::TypeKind::Integer) {
        return "LOAD";
    }
    switch (type->bits) {
    case 8:
        return type->is_signed ? "LOAD8S" : "LOAD8U";
    case 16:
        return type->is_signed ? "LOAD16S" : "LOAD16U";
    default:
        return "LOAD";
    }
}

std::string store_opcode(ir::TypePtr type) {
    if (!type || type->kind != ir::TypeKind::Integer) {
        return "STORE";
    }
    switch (type->bits) {
    case 8:
        return "STORE8";
    case 16:
        return "STORE16";
    default:
        return "STORE";
    }
}

std::string condition_for(ir::ICmpPredicate predicate) {
    switch (predicate) {
    case ir::ICmpPredicate::Eq:
        return "JZ";
    case ir::ICmpPredicate::Ne:
        return "JNZ";
    case ir::ICmpPredicate::Ugt:
        return "JHI";
    case ir::ICmpPredicate::Uge:
        return "JCS";
    case ir::ICmpPredicate::Ult:
        return "JCC";
    case ir::ICmpPredicate::Ule:
        return "JLS";
    case ir::ICmpPredicate::Sgt:
        return "JGT";
    case ir::ICmpPredicate::Sge:
        return "JGE";
    case ir::ICmpPredicate::Slt:
        return "JLT";
    case ir::ICmpPredicate::Sle:
        return "JLE";
    }
    return "JMP";
}

}  // namespace

Kr32Backend::Kr32Backend(const ir::Module& module, const target::TargetProfile& profile)
    : module_(module), profile_(profile) {}

std::uint64_t Kr32Backend::size_of(ir::TypePtr type, std::string& error) const {
    if (!type) {
        error = "missing IR type in backend";
        return 0;
    }
    switch (type->kind) {
    case ir::TypeKind::Void:
        return 0;
    case ir::TypeKind::Integer:
    case ir::TypeKind::Float:
        return type->bits == 0 ? (type->kind == ir::TypeKind::Float ? 8 : 4)
                               : (type->bits + 7) / 8;
    case ir::TypeKind::Pointer:
        return profile_.pointer_bits / 8;
    case ir::TypeKind::Array:
        return size_of(type->element, error) * type->count;
    case ir::TypeKind::Struct: {
        std::uint64_t offset = 0;
        std::uint64_t alignment = 1;
        for (const auto field : type->fields) {
            const auto field_alignment = align_of(field);
            alignment = std::max(alignment, field_alignment);
            offset = (offset + field_alignment - 1) / field_alignment * field_alignment;
            offset += size_of(field, error);
        }
        return (offset + alignment - 1) / alignment * alignment;
    }
    case ir::TypeKind::Union: {
        std::uint64_t size = 0;
        for (const auto field : type->fields) {
            size = std::max(size, size_of(field, error));
        }
        return size;
    }
    case ir::TypeKind::Function:
        error = "function values have no object size";
        return 0;
    }
    return 0;
}

std::uint64_t Kr32Backend::align_of(ir::TypePtr type) const {
    if (!type) {
        return 1;
    }
    switch (type->kind) {
    case ir::TypeKind::Void:
        return 1;
    case ir::TypeKind::Integer:
    case ir::TypeKind::Float:
        return std::max<std::uint64_t>(1, (type->bits + 7) / 8);
    case ir::TypeKind::Pointer:
        return profile_.pointer_bits / 8;
    case ir::TypeKind::Array:
        return align_of(type->element);
    case ir::TypeKind::Struct:
    case ir::TypeKind::Union: {
        std::uint64_t alignment = 1;
        for (const auto field : type->fields) {
            alignment = std::max(alignment, align_of(field));
        }
        return alignment;
    }
    case ir::TypeKind::Function:
        return 1;
    }
    return 1;
}

std::string Kr32Backend::block_label(const ir::Function& function,
                                     const ir::BasicBlock& block) const {
    return ".L" + function.name + "." + block.name;
}

std::string Kr32Backend::escape_string(const std::vector<std::uint8_t>& bytes) const {
    std::string result;
    for (const auto byte : bytes) {
        if (!result.empty()) {
            result += ", ";
        }
        result += std::to_string(static_cast<unsigned>(byte));
    }
    return result;
}

Kr32Backend::FrameLayout Kr32Backend::layout_function(const ir::Function& function) const {
    FrameLayout layout;
    std::string error;

    for (const auto& block : function.blocks) {
        for (const auto& instruction : block->instructions) {
            if (instruction->opcode != ir::Opcode::Call || instruction->operands.empty()) {
                continue;
            }
            auto callee_type = instruction->operands[0]->type;
            if (callee_type && callee_type->kind == ir::TypeKind::Pointer) {
                callee_type = callee_type->element;
            }
            if (!callee_type || callee_type->kind != ir::TypeKind::Function) {
                continue;
            }
            const auto fixed = callee_type->parameters.size();
            if (instruction->operands.size() > fixed + 1) {
                const auto extras = instruction->operands.size() - fixed - 1;
                layout.outgoing_size = std::max<std::uint32_t>(
                    layout.outgoing_size, static_cast<std::uint32_t>(extras * 8));
            }
        }
    }

    std::uint32_t offset = layout.outgoing_size;
    layout.float_temp = offset;
    offset += 8;
    for (const auto& parameter : function.parameters) {
        const auto alignment = static_cast<std::uint32_t>(align_of(parameter->type));
        offset = static_cast<std::uint32_t>(round_up(offset, alignment));
        layout.slots.emplace(parameter.get(), offset);
        offset += static_cast<std::uint32_t>(std::max<std::uint64_t>(4, size_of(parameter->type, error)));
    }

    for (const auto& block : function.blocks) {
        for (const auto& instruction : block->instructions) {
            if (instruction->opcode == ir::Opcode::Alloca) {
                const auto object_size = size_of(instruction->allocated_type, error);
                const auto object_alignment = align_of(instruction->allocated_type);
                offset = static_cast<std::uint32_t>(round_up(offset, object_alignment));
                layout.alloca_offsets.emplace(instruction.get(), offset);
                offset += static_cast<std::uint32_t>(object_size);
            }
            if (!instruction->type || ir::is_void(instruction->type)) {
                continue;
            }
            const auto alignment = static_cast<std::uint32_t>(align_of(instruction->type));
            offset = static_cast<std::uint32_t>(round_up(offset, alignment));
            layout.slots.emplace(instruction.get(), offset);
            offset += static_cast<std::uint32_t>(
                std::max<std::uint64_t>(ir::is_float(instruction->type) ? 8 : 4,
                                        size_of(instruction->type, error)));
        }
    }
    layout.frame_size = static_cast<std::uint32_t>(round_up(offset, 8));
    return layout;
}

std::optional<std::uint32_t> Kr32Backend::value_slot(
    const ir::Function& function, const FrameLayout& layout, const ir::Value* value,
    std::string& error) const {
    static_cast<void>(function);
    const auto found = layout.slots.find(value);
    if (found == layout.slots.end()) {
        error = "value '" + value->name + "' has no frame slot";
        return std::nullopt;
    }
    return found->second;
}

void Kr32Backend::load_value(std::string& out, const ir::Function& function,
                             const FrameLayout& layout, const ir::Value* value,
                             std::string_view reg) const {
    if (!value) {
        out += "        MOV " + std::string(reg) + ", #0\n";
        return;
    }
    switch (value->kind) {
    case ir::ValueKind::ConstantInt:
        out += "        MOV " + std::string(reg) + ", #" +
               std::to_string(static_cast<const ir::ConstantInt*>(value)->value) + "\n";
        return;
    case ir::ValueKind::ConstantNull:
    case ir::ValueKind::ConstantUndef:
        out += "        MOV " + std::string(reg) + ", #0\n";
        return;
    case ir::ValueKind::Function:
        out += "        LEA " + std::string(reg) + ", [" + value->name + "]\n";
        return;
    case ir::ValueKind::Global:
        out += "        LEA " + std::string(reg) + ", [" + value->name + "]\n";
        return;
    case ir::ValueKind::ConstantFloat:
    case ir::ValueKind::ConstantDataArray:
        out += "        MOV " + std::string(reg) + ", #0 ; unsupported constant\n";
        return;
    case ir::ValueKind::Parameter:
    case ir::ValueKind::Instruction:
        break;
    }
    if (value->kind == ir::ValueKind::Instruction) {
        const auto* instruction = static_cast<const ir::Instruction*>(value);
        if (instruction->opcode == ir::Opcode::Alloca) {
            const auto object = layout.alloca_offsets.find(instruction);
            if (object != layout.alloca_offsets.end()) {
                out += "        LEA " + std::string(reg) + ", [R7 + " +
                       std::to_string(object->second) + "]\n";
                return;
            }
        }
    }
    std::string error;
    const auto slot = value_slot(function, layout, value, error);
    if (!slot) {
        out += "; " + error + "\n";
        out += "        MOV " + std::string(reg) + ", #0\n";
        return;
    }
    out += "        LOAD " + std::string(reg) + ", [R7 + " + std::to_string(*slot) + "]\n";
}

void Kr32Backend::store_value(std::string& out, const ir::Function& function,
                              const FrameLayout& layout, const ir::Instruction& instruction,
                              std::string_view reg) const {
    std::string error;
    const auto slot = value_slot(function, layout, &instruction, error);
    if (!slot) {
        out += "; " + error + "\n";
        return;
    }
    out += "        STORE " + std::string(reg) + ", [R7 + " + std::to_string(*slot) + "]\n";
}

void Kr32Backend::load_fp(std::string& out, const ir::Function& function,
                          const FrameLayout& layout, const ir::Value* value,
                          std::string_view reg) const {
    if (!value) {
        out += "        MOV R0, #0\n";
        out += "        MOV R1, #0\n";
        out += "        STORE R0, [R7 + " + std::to_string(layout.float_temp) + "]\n";
        out += "        STORE R1, [R7 + " + std::to_string(layout.float_temp + 4) + "]\n";
        out += "        FLOAD.D " + std::string(reg) + ", [R7 + " +
               std::to_string(layout.float_temp) + "]\n";
        return;
    }
    if (value->kind == ir::ValueKind::ConstantFloat) {
        const auto number = static_cast<const ir::ConstantFloat*>(value)->value;
        std::uint64_t bits = 0;
        if (value->type && value->type->bits == 32) {
            const auto narrowed = static_cast<float>(number);
            std::uint32_t narrow_bits = 0;
            std::memcpy(&narrow_bits, &narrowed, sizeof(narrow_bits));
            bits = narrow_bits;
        } else {
            std::memcpy(&bits, &number, sizeof(bits));
        }
        out += "        MOV R0, #" + std::to_string(static_cast<std::int32_t>(bits)) + "\n";
        out += "        MOV R1, #" + std::to_string(static_cast<std::int32_t>(bits >> 32)) +
               "\n";
        out += "        STORE R0, [R7 + " + std::to_string(layout.float_temp) + "]\n";
        out += "        STORE R1, [R7 + " + std::to_string(layout.float_temp + 4) + "]\n";
        out += "        " +
               std::string(value->type && value->type->bits == 32 ? "FLOAD.W" : "FLOAD.D") +
               " " + std::string(reg) + ", [R7 + " +
               std::to_string(layout.float_temp) + "]\n";
        return;
    }
    if (value->kind == ir::ValueKind::ConstantNull ||
        value->kind == ir::ValueKind::ConstantUndef) {
        out += "        MOV R0, #0\n";
        out += "        MOV R1, #0\n";
        out += "        STORE R0, [R7 + " + std::to_string(layout.float_temp) + "]\n";
        out += "        STORE R1, [R7 + " + std::to_string(layout.float_temp + 4) + "]\n";
        out += "        FLOAD.D " + std::string(reg) + ", [R7 + " +
               std::to_string(layout.float_temp) + "]\n";
        return;
    }
    std::string error;
    const auto slot = value_slot(function, layout, value, error);
    if (!slot) {
        out += "; " + error + "\n";
        return;
    }
    const auto width = value->type && value->type->bits == 32 ? "FLOAD.W" : "FLOAD.D";
    out += "        " + std::string(width) + " " + std::string(reg) + ", [R7 + " +
           std::to_string(*slot) + "]\n";
}

void Kr32Backend::add_debug_type(ir::TypePtr type) const {
    if (!type || ir::is_void(type)) {
        return;
    }
    auto name = type_to_string(type);
    name.erase(std::remove(name.begin(), name.end(), '%'), name.end());
    std::replace(name.begin(), name.end(), ' ', '_');
    if (!debug_types_.insert(name).second) {
        return;
    }
    std::string error;
    const auto size = size_of(type, error);
    if (!error.empty()) {
        return;
    }
    debug_info_ += "T " + name + " " + std::to_string(size) + std::string(1, '\0');
    if (type->kind == ir::TypeKind::Struct) {
        debug_info_ += "C " + name + " " + std::to_string(size) + " " +
                       std::string(1, '\0');
        std::uint64_t offset = 0;
        for (std::size_t index = 0; index < type->fields.size(); ++index) {
            const auto& field = type->fields[index];
            const auto alignment = align_of(field);
            offset = round_up(offset, alignment);
            std::string field_error;
            const auto field_size = size_of(field, field_error);
            if (!field_error.empty()) {
                continue;
            }
            const auto field_name = index < type->field_names.size()
                                        ? type->field_names[index]
                                        : std::to_string(index);
            debug_info_ += "M " + name + " " + field_name + " " + std::to_string(offset) +
                           " " + [&] {
                               auto field_type = type_to_string(field);
                               field_type.erase(std::remove(field_type.begin(), field_type.end(),
                                                            '%'),
                                               field_type.end());
                               std::replace(field_type.begin(), field_type.end(), ' ', '_');
                               return field_type;
                           }() + " " + std::to_string(field_size) +
                           std::string(1, '\0');
            add_debug_type(field);
            offset += field_size;
        }
    } else if (type->kind == ir::TypeKind::Array) {
        add_debug_type(type->element);
    } else if (type->kind == ir::TypeKind::Pointer) {
        add_debug_type(type->element);
    }
}

void Kr32Backend::store_fp(std::string& out, const ir::Function& function,
                           const FrameLayout& layout, const ir::Instruction& instruction,
                           std::string_view reg) const {
    std::string error;
    const auto slot = value_slot(function, layout, &instruction, error);
    if (!slot) {
        out += "; " + error + "\n";
        return;
    }
    const auto width = instruction.type && instruction.type->bits == 32 ? "FSTORE.W" : "FSTORE.D";
    out += "        " + std::string(width) + " " + std::string(reg) + ", [R7 + " +
           std::to_string(*slot) + "]\n";
}

std::optional<std::string> Kr32Backend::emit(std::string& error) const {
    std::string out = "; KR32 assembly generated by krc\n";
    debug_info_.clear();
    debug_sources_.clear();
    debug_types_.clear();
    next_debug_source_ = 0;

    for (const auto& global : module_.globals()) {
        out += "        .data\n";
        out += "        .align 4\n";
        out += global->name + ":\n";
        if (global->name.empty() || global->name.front() != '.') {
            out += "        .global " + global->name + "\n";
        }
        if (global->initializer &&
            global->initializer->kind == ir::ValueKind::ConstantDataArray) {
            const auto* data = static_cast<const ir::ConstantDataArray*>(global->initializer);
            out += "        .byte " + escape_string(data->bytes) + "\n";
        } else if (global->initializer &&
                   global->initializer->kind == ir::ValueKind::ConstantInt) {
            out += "        .word " +
                   std::to_string(static_cast<const ir::ConstantInt*>(global->initializer)->value) +
                   "\n";
        } else {
            const auto size = size_of(global->element_type, error);
            if (!error.empty()) {
                return std::nullopt;
            }
            out += "        .zero " + std::to_string(size) + "\n";
        }
    }

    for (const auto& function : module_.functions()) {
        if (function->declaration) {
            out += "        .extern " + function->name + "\n";
        }
    }
    out += "        .text\n";
    for (const auto& function : module_.functions()) {
        if (function->declaration) {
            continue;
        }
        const auto layout = layout_function(*function);
        emit_function(out, *function, layout);
    }
    for (const auto& global : module_.globals()) {
        add_debug_type(global->element_type);
        std::string size_error;
        const auto size = size_of(global->element_type, size_error);
        if (!size_error.empty()) {
            continue;
        }
        debug_info_ += "G " + global->name + " " + global->name + " " +
                       [&] {
                           auto global_type = type_to_string(global->element_type);
                           global_type.erase(std::remove(global_type.begin(), global_type.end(),
                                                         '%'),
                                             global_type.end());
                           std::replace(global_type.begin(), global_type.end(), ' ', '_');
                           return global_type;
                       }() + " " + std::to_string(size) + std::string(1, '\0');
    }
    return peephole(out);
}

void Kr32Backend::emit_function(std::string& out, const ir::Function& function,
                                const FrameLayout& layout) const {
    if (function.linkage == ir::Linkage::External) {
        out += "        .global " + function.name + "\n";
    } else if (function.linkage == ir::Linkage::Weak) {
        out += "        .weak " + function.name + "\n";
    }
    out += function.name + ":\n";
    out += "        PUSH R7\n";
    out += "        ADJSP #-" + std::to_string(layout.frame_size) + "\n";
    out += "        LEA R7, [SP]\n";

    std::size_t debug_label_counter = 0;
    std::size_t integer_parameter = 0;
    std::size_t fp_parameter = 0;
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        const auto* parameter = function.parameters[index].get();
        const auto slot = layout.slots.find(parameter);
        if (slot == layout.slots.end()) {
            continue;
        }
        if (ir::is_float(parameter->type)) {
            if (fp_parameter < 8) {
                const auto width = parameter->type->bits == 32 ? "FSTORE.W" : "FSTORE.D";
                out += "        " + std::string(width) + " F" +
                       std::to_string(fp_parameter) + ", [R7 + " +
                       std::to_string(slot->second) + "]\n";
            }
            fp_parameter++;
        } else if (parameter->type &&
                   parameter->type->kind == ir::TypeKind::Struct) {
            std::string size_error;
            const auto size = size_of(parameter->type, size_error);
            const auto words = (size + 3) / 4;
            if (!size_error.empty() || integer_parameter + words > 4) {
                out += "        ; aggregate parameter unavailable\n";
                continue;
            }
            for (std::uint64_t word = 0; word < words; ++word) {
                out += "        STORE " +
                       std::string(register_name(integer_parameter + word)) +
                       ", [R7 + " +
                       std::to_string(slot->second + word * 4) + "]\n";
            }
            integer_parameter += words;
        } else if (integer_parameter < 4) {
            out += "        STORE " + register_name(integer_parameter) + ", [R7 + " +
                   std::to_string(slot->second) + "]\n";
            integer_parameter++;
        } else {
            out += "        LOAD R0, [R7 + " + std::to_string(layout.frame_size + 8 +
                                                               (integer_parameter - 4) * 4) +
                   "]\n";
            out += "        STORE R0, [R7 + " + std::to_string(slot->second) + "]\n";
            integer_parameter++;
        }
    }

    for (const auto& block : function.blocks) {
        out += block_label(function, *block) + ":\n";
        for (const auto& instruction : block->instructions) {
            if (instruction->debug.valid()) {
                auto source = debug_sources_.find(instruction->debug.file);
                if (source == debug_sources_.end()) {
                    const auto id = next_debug_source_++;
                    source = debug_sources_.emplace(instruction->debug.file, id).first;
                    debug_info_ += "S " + std::to_string(id) + " " + instruction->debug.file +
                                   std::string(1, '\0');
                }
                const auto label = ".Ldbg." + function.name + "." +
                                   std::to_string(debug_label_counter++);
                out += label + ":\n";
                debug_info_ += "L " + label + " " + std::to_string(source->second) + " " +
                               std::to_string(instruction->debug.line) + " " +
                               std::to_string(instruction->debug.column) + std::string(1, '\0');
            }
            emit_instruction(out, function, layout, *instruction);
        }
    }

    debug_info_ += "F " + function.name + " " + function.name + " .L" + function.name +
                   ".ret " + std::to_string(layout.frame_size) + " " +
                   [&] {
                       auto result = type_to_string(function.type ? function.type->return_type
                                                                 : nullptr);
                       result.erase(std::remove(result.begin(), result.end(), '%'), result.end());
                       std::replace(result.begin(), result.end(), ' ', '_');
                       return result;
                   }() + std::string(1, '\0');
    std::unordered_set<std::string> debug_variable_names;
    for (const auto& parameter : function.parameters) {
        const auto slot = layout.slots.find(parameter.get());
        if (slot == layout.slots.end() || !parameter->type) {
            continue;
        }
        if (!debug_variable_names.insert(function.name + "." + parameter->name).second) {
            continue;
        }
        std::string size_error;
        const auto size = size_of(parameter->type, size_error);
        if (!size_error.empty()) {
            continue;
        }
        add_debug_type(parameter->type);
        auto parameter_type = type_to_string(parameter->type);
        parameter_type.erase(std::remove(parameter_type.begin(), parameter_type.end(), '%'),
                             parameter_type.end());
        std::replace(parameter_type.begin(), parameter_type.end(), ' ', '_');
        debug_info_ += "V " + function.name + " " + parameter->name + " " +
                       std::to_string(slot->second) + " " + parameter_type + " " +
                       std::to_string(size) + std::string(1, '\0');
    }
    std::unordered_set<std::string> parameter_names;
    for (const auto& parameter : function.parameters) {
        parameter_names.insert(parameter->name);
    }
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block->instructions) {
            if (instruction->opcode != ir::Opcode::Alloca || !instruction->allocated_type) {
                continue;
            }
            const auto object = layout.alloca_offsets.find(instruction.get());
            if (object == layout.alloca_offsets.end()) {
                continue;
            }
            std::string size_error;
            const auto size = size_of(instruction->allocated_type, size_error);
            if (!size_error.empty()) {
                continue;
            }
            auto name = instruction->name;
            if (name.size() > 5 && name.compare(name.size() - 5, 5, ".addr") == 0) {
                name.resize(name.size() - 5);
            }
            if (parameter_names.contains(name)) {
                continue;
            }
            if (!debug_variable_names.insert(function.name + "." + name).second) {
                continue;
            }
            add_debug_type(instruction->allocated_type);
            auto variable_type = type_to_string(instruction->allocated_type);
            variable_type.erase(std::remove(variable_type.begin(), variable_type.end(), '%'),
                                variable_type.end());
            std::replace(variable_type.begin(), variable_type.end(), ' ', '_');
            debug_info_ += "V " + function.name + " " + name + " " +
                           std::to_string(object->second) + " " + variable_type + " " +
                           std::to_string(size) + std::string(1, '\0');
        }
    }

    out += ".L" + function.name + ".ret:\n";
    out += "        ADJSP #" + std::to_string(layout.frame_size) + "\n";
    out += "        POP R7\n";
    out += "        RET\n";
}

void Kr32Backend::emit_instruction(std::string& out, const ir::Function& function,
                                   const FrameLayout& layout,
                                   const ir::Instruction& instruction) const {
    const auto binary_instruction = [&](std::string_view mnemonic) {
        load_value(out, function, layout, instruction.operands[0], "R0");
        load_value(out, function, layout, instruction.operands[1], "R1");
        out += "        " + std::string(mnemonic) + " R0, R0, R1\n";
        store_value(out, function, layout, instruction, "R0");
    };

    switch (instruction.opcode) {
    case ir::Opcode::FAdd:
        load_fp(out, function, layout, instruction.operands[0], "F0");
        load_fp(out, function, layout, instruction.operands[1], "F1");
        out += "        FADD." +
               std::string(instruction.operands[0]->type->bits == 32 ? "S" : "D") +
               " F0, F0, F1\n";
        store_fp(out, function, layout, instruction, "F0");
        break;
    case ir::Opcode::FSub:
        load_fp(out, function, layout, instruction.operands[0], "F0");
        load_fp(out, function, layout, instruction.operands[1], "F1");
        out += "        FSUB." +
               std::string(instruction.operands[0]->type->bits == 32 ? "S" : "D") +
               " F0, F0, F1\n";
        store_fp(out, function, layout, instruction, "F0");
        break;
    case ir::Opcode::FMul:
        load_fp(out, function, layout, instruction.operands[0], "F0");
        load_fp(out, function, layout, instruction.operands[1], "F1");
        out += "        FMUL." +
               std::string(instruction.operands[0]->type->bits == 32 ? "S" : "D") +
               " F0, F0, F1\n";
        store_fp(out, function, layout, instruction, "F0");
        break;
    case ir::Opcode::FDiv:
        load_fp(out, function, layout, instruction.operands[0], "F0");
        load_fp(out, function, layout, instruction.operands[1], "F1");
        out += "        FDIV." +
               std::string(instruction.operands[0]->type->bits == 32 ? "S" : "D") +
               " F0, F0, F1\n";
        store_fp(out, function, layout, instruction, "F0");
        break;
    case ir::Opcode::FCmp:
        load_fp(out, function, layout, instruction.operands[0], "F0");
        load_fp(out, function, layout, instruction.operands[1], "F1");
        {
        const auto suffix =
            std::string(instruction.operands[0]->type->bits == 32 ? "S" : "D");
        switch (instruction.fcmp_predicate) {
        case ir::FCmpPredicate::Oeq:
            out += "        FEQ." + suffix + " R0, F0, F1\n";
            break;
        case ir::FCmpPredicate::One:
            out += "        FEQ." + suffix + " R0, F0, F1\n";
            out += "        XOR R0, R0, #1\n";
            break;
        case ir::FCmpPredicate::Olt:
            out += "        FLT." + suffix + " R0, F0, F1\n";
            break;
        case ir::FCmpPredicate::Ole:
            out += "        FLE." + suffix + " R0, F0, F1\n";
            break;
        case ir::FCmpPredicate::Ogt:
            out += "        FLT." + suffix + " R0, F1, F0\n";
            break;
        case ir::FCmpPredicate::Oge:
            out += "        FLE." + suffix + " R0, F1, F0\n";
            break;
        case ir::FCmpPredicate::Ord:
            out += "        FEQ." + suffix + " R0, F0, F0\n";
            break;
        case ir::FCmpPredicate::Uno:
            out += "        FEQ." + suffix + " R0, F0, F0\n";
            out += "        XOR R0, R0, #1\n";
            break;
        case ir::FCmpPredicate::False:
            out += "        MOV R0, #0\n";
            break;
        case ir::FCmpPredicate::True:
            out += "        MOV R0, #1\n";
            break;
        }
        }
        store_value(out, function, layout, instruction, "R0");
        break;
    case ir::Opcode::Add:
        binary_instruction("ADD");
        break;
    case ir::Opcode::Sub:
        binary_instruction("SUB");
        break;
    case ir::Opcode::Mul:
        binary_instruction("MUL");
        break;
    case ir::Opcode::SDiv:
        binary_instruction("DIVS");
        break;
    case ir::Opcode::UDiv:
        binary_instruction("DIV");
        break;
    case ir::Opcode::SRem:
        binary_instruction("MODS");
        break;
    case ir::Opcode::URem:
        binary_instruction("MOD");
        break;
    case ir::Opcode::And:
        binary_instruction("AND");
        break;
    case ir::Opcode::Or:
        binary_instruction("OR");
        break;
    case ir::Opcode::Xor:
        binary_instruction("XOR");
        break;
    case ir::Opcode::Shl:
        binary_instruction("SHL");
        break;
    case ir::Opcode::LShr:
        binary_instruction("SHR");
        break;
    case ir::Opcode::AShr:
        binary_instruction("SAR");
        break;
    case ir::Opcode::Alloca: {
        const auto object = layout.alloca_offsets.find(&instruction);
        if (object == layout.alloca_offsets.end()) {
            out += "        HALT ; missing alloca layout\n";
            return;
        }
        out += "        LEA R0, [R7 + " + std::to_string(object->second) + "]\n";
        store_value(out, function, layout, instruction, "R0");
        break;
    }
    case ir::Opcode::VaStart:
        out += "        LEA R0, [R7 + " +
               std::to_string(layout.frame_size + 8) + "]\n";
        store_value(out, function, layout, instruction, "R0");
        break;
    case ir::Opcode::Load: {
        load_value(out, function, layout, instruction.operands[0], "R0");
        if (ir::is_float(instruction.type)) {
            out += "        " +
                   std::string(instruction.type->bits == 32 ? "FLOAD.W" : "FLOAD.D") +
                   " F0, [R0]\n";
            store_fp(out, function, layout, instruction, "F0");
        } else if (instruction.type &&
                   instruction.type->kind == ir::TypeKind::Struct) {
            std::string size_error;
            const auto size = size_of(instruction.type, size_error);
            const auto slot = layout.slots.find(&instruction);
            if (!size_error.empty() || slot == layout.slots.end()) {
                out += "        ; aggregate load unavailable\n";
                break;
            }
            out += "        MOV R1, R0\n";
            for (std::uint64_t offset = 0; offset < size; offset += 4) {
                out += "        LOAD R0, [R1 + " + std::to_string(offset) + "]\n";
                out += "        STORE R0, [R7 + " +
                       std::to_string(slot->second + offset) + "]\n";
            }
        } else {
            out += "        " + load_opcode(instruction.type) + " R0, [R0]\n";
            store_value(out, function, layout, instruction, "R0");
        }
        break;
    }
    case ir::Opcode::Store:
        if (ir::is_float(instruction.operands[0]->type)) {
            load_fp(out, function, layout, instruction.operands[0], "F0");
            load_value(out, function, layout, instruction.operands[1], "R1");
            out += "        " +
                   std::string(instruction.operands[0]->type->bits == 32 ? "FSTORE.W"
                                                                         : "FSTORE.D") +
                   " F0, [R1]\n";
        } else if (instruction.operands[0]->type &&
                   instruction.operands[0]->type->kind == ir::TypeKind::Struct) {
            const auto* source = instruction.operands[0];
            const auto slot = layout.slots.find(source);
            std::string size_error;
            const auto size = size_of(source->type, size_error);
            if (!size_error.empty() || slot == layout.slots.end()) {
                out += "        ; aggregate store unavailable\n";
                break;
            }
            load_value(out, function, layout, instruction.operands[1], "R1");
            for (std::uint64_t offset = 0; offset < size; offset += 4) {
                out += "        LOAD R0, [R7 + " +
                       std::to_string(slot->second + offset) + "]\n";
                out += "        STORE R0, [R1 + " + std::to_string(offset) + "]\n";
            }
        } else {
            load_value(out, function, layout, instruction.operands[0], "R0");
            load_value(out, function, layout, instruction.operands[1], "R1");
            out += "        " + store_opcode(instruction.operands[0]->type) + " R0, [R1]\n";
        }
        break;
    case ir::Opcode::GetElementPtr: {
        load_value(out, function, layout, instruction.operands[0], "R0");
        ir::TypePtr current = instruction.source_type;
        for (std::size_t index = 1; index < instruction.operands.size(); ++index) {
            std::string error;
            std::uint64_t stride = 1;
            if (current && (current->kind == ir::TypeKind::Struct ||
                            current->kind == ir::TypeKind::Union) &&
                index > 1) {
                const auto* constant =
                    dynamic_cast<const ir::ConstantInt*>(instruction.operands[index]);
                if (!constant || constant->value < 0 ||
                    static_cast<std::size_t>(constant->value) >= current->fields.size()) {
                    out += "        HALT ; invalid aggregate member index\n";
                    return;
                }
                std::uint64_t offset = 0;
                if (current->kind == ir::TypeKind::Struct) {
                    std::uint64_t alignment = 1;
                    for (std::int64_t field = 0; field < constant->value; ++field) {
                        const auto field_alignment =
                            align_of(current->fields[static_cast<std::size_t>(field)]);
                        alignment = std::max(alignment, field_alignment);
                        offset = (offset + field_alignment - 1) / field_alignment *
                                 field_alignment;
                        offset += size_of(current->fields[static_cast<std::size_t>(field)],
                                          error);
                    }
                    const auto field_alignment =
                        align_of(current->fields[static_cast<std::size_t>(constant->value)]);
                    offset = (offset + field_alignment - 1) / field_alignment * field_alignment;
                }
                if (!error.empty()) {
                    out += "        HALT ; " + error + "\n";
                    return;
                }
                current = current->fields[static_cast<std::size_t>(constant->value)];
                if (offset != 0) {
                    out += "        ADD R0, R0, #" + std::to_string(offset) + "\n";
                }
                continue;
            }
            if (index == 1) {
                stride = size_of(current, error);
            } else if (current && current->kind == ir::TypeKind::Array) {
                current = current->element;
                stride = size_of(current, error);
            } else if (current && current->kind == ir::TypeKind::Pointer) {
                current = current->element;
                stride = size_of(current, error);
            }
            if (!error.empty()) {
                out += "        HALT ; " + error + "\n";
                return;
            }
            load_value(out, function, layout, instruction.operands[index], "R1");
            if (stride != 1) {
                out += "        MUL R1, R1, #" + std::to_string(stride) + "\n";
            }
            out += "        ADD R0, R0, R1\n";
        }
        store_value(out, function, layout, instruction, "R0");
        break;
    }
    case ir::Opcode::ICmp: {
        load_value(out, function, layout, instruction.operands[0], "R0");
        load_value(out, function, layout, instruction.operands[1], "R1");
        out += "        CMP R0, R1\n";
        out += "        MOV R2, #0\n";
        const auto true_label = ".L" + function.name + "." + instruction.name + ".true";
        const auto done_label = ".L" + function.name + "." + instruction.name + ".done";
        out += "        " + condition_for(instruction.icmp_predicate) + " " + true_label + "\n";
        out += "        JMP " + done_label + "\n";
        out += true_label + ":\n";
        out += "        MOV R2, #1\n";
        out += done_label + ":\n";
        store_value(out, function, layout, instruction, "R2");
        break;
    }
    case ir::Opcode::Br:
        out += "        JMP " +
               block_label(function, *instruction.successors.front()) + "\n";
        break;
    case ir::Opcode::CondBr:
        load_value(out, function, layout, instruction.operands[0], "R0");
        out += "        CMP R0, #0\n";
        out += "        JNZ " + block_label(function, *instruction.successors[0]) + "\n";
        out += "        JMP " + block_label(function, *instruction.successors[1]) + "\n";
        break;
    case ir::Opcode::Ret:
        if (!instruction.operands.empty()) {
            if (ir::is_float(instruction.operands[0]->type)) {
                load_fp(out, function, layout, instruction.operands[0], "F0");
            } else {
                load_value(out, function, layout, instruction.operands[0], "R0");
            }
        }
        out += "        JMP .L" + function.name + ".ret\n";
        break;
    case ir::Opcode::Call: {
        auto callee_type = instruction.operands[0]->type;
        if (callee_type && callee_type->kind == ir::TypeKind::Pointer) {
            callee_type = callee_type->element;
        }
        if (!callee_type || callee_type->kind != ir::TypeKind::Function) {
            out += "        HALT ; invalid call target\n";
            return;
        }
        const auto fixed = callee_type->parameters.size();
        const auto total = instruction.operands.size() - 1;
        for (std::size_t index = fixed; index < total; ++index) {
            const auto* argument = instruction.operands[index + 1];
            const auto offset = static_cast<std::uint32_t>((index - fixed) * 8);
            if (argument->type && ir::is_float(argument->type)) {
                load_fp(out, function, layout, argument, "F0");
                out += "        FSTORE.D F0, [R7 + " + std::to_string(offset) + "]\n";
            } else {
                load_value(out, function, layout, argument, "R0");
                out += "        STORE R0, [R7 + " + std::to_string(offset) + "]\n";
                out += "        MOV R0, #0\n";
                out += "        STORE R0, [R7 + " + std::to_string(offset + 4) + "]\n";
            }
        }
        std::size_t integer_index = 0;
        std::size_t fp_index = 0;
        for (std::size_t index = 0; index < fixed; ++index) {
            const auto* argument = instruction.operands[index + 1];
            if (argument->type && ir::is_float(argument->type)) {
                load_fp(out, function, layout, argument, "F" + std::to_string(fp_index++));
            } else if (argument->type &&
                       argument->type->kind == ir::TypeKind::Struct) {
                const auto slot = layout.slots.find(argument);
                std::string size_error;
                const auto size = size_of(argument->type, size_error);
                const auto words = (size + 3) / 4;
                if (!size_error.empty() || slot == layout.slots.end() ||
                    integer_index + words > 4) {
                    out += "        ; aggregate argument unavailable\n";
                    continue;
                }
                for (std::uint64_t word = 0; word < words; ++word) {
                    out += "        LOAD " +
                           std::string(register_name(integer_index)) +
                           ", [R7 + " +
                           std::to_string(slot->second + word * 4) + "]\n";
                    integer_index++;
                }
            } else {
                load_value(out, function, layout, argument,
                           register_name(integer_index++));
            }
        }
        if (instruction.operands[0]->kind == ir::ValueKind::Function) {
            out += "        CALL " + instruction.operands[0]->name + "\n";
        } else {
            load_value(out, function, layout, instruction.operands[0], "R4");
            out += "        CALL R4\n";
        }
        if (instruction.type && !ir::is_void(instruction.type)) {
            if (ir::is_float(instruction.type)) {
                store_fp(out, function, layout, instruction, "F0");
            } else {
                store_value(out, function, layout, instruction, "R0");
            }
        }
        break;
    }
    case ir::Opcode::FPTrunc:
        load_fp(out, function, layout, instruction.operands[0], "F0");
        out += "        FCVT.S.D F0, F0\n";
        store_fp(out, function, layout, instruction, "F0");
        break;
    case ir::Opcode::FPExt:
        load_fp(out, function, layout, instruction.operands[0], "F0");
        out += "        FCVT.D.S F0, F0\n";
        store_fp(out, function, layout, instruction, "F0");
        break;
    case ir::Opcode::FPToSI:
    case ir::Opcode::FPToUI:
        load_fp(out, function, layout, instruction.operands[0], "F0");
        out += "        FCVT.W." +
               std::string(instruction.operands[0]->type->bits == 32 ? "S" : "D") +
               " R0, F0\n";
        store_value(out, function, layout, instruction, "R0");
        break;
    case ir::Opcode::SIToFP:
    case ir::Opcode::UIToFP:
        load_value(out, function, layout, instruction.operands[0], "R0");
        out += "        FCVT." +
               std::string(instruction.type->bits == 32 ? "S" : "D") + ".W F0, R0\n";
        store_fp(out, function, layout, instruction, "F0");
        break;
    case ir::Opcode::Trunc:
    case ir::Opcode::ZExt:
    case ir::Opcode::SExt: {
        load_value(out, function, layout, instruction.operands[0], "R0");
        const auto source_bits = instruction.operands[0]->type
                                     ? instruction.operands[0]->type->bits
                                     : 32;
        if (instruction.opcode == ir::Opcode::SExt && source_bits < 32) {
            out += "        SHL R0, R0, #" + std::to_string(32 - source_bits) + "\n";
            out += "        SAR R0, R0, #" + std::to_string(32 - source_bits) + "\n";
        } else if (instruction.opcode == ir::Opcode::ZExt && source_bits < 32) {
            out += "        AND R0, R0, #" + std::to_string((1ull << source_bits) - 1) + "\n";
        } else if (instruction.opcode == ir::Opcode::Trunc && instruction.type &&
                   instruction.type->bits < 32) {
            out += "        AND R0, R0, #" +
                   std::to_string((1ull << instruction.type->bits) - 1) + "\n";
        }
        store_value(out, function, layout, instruction, "R0");
        break;
    }
    case ir::Opcode::BitCast:
    case ir::Opcode::PtrToInt:
    case ir::Opcode::IntToPtr:
        load_value(out, function, layout, instruction.operands[0], "R0");
        store_value(out, function, layout, instruction, "R0");
        break;
    case ir::Opcode::Unreachable:
        out += "        HALT\n";
        break;
    default:
        out += "        HALT ; unsupported IR opcode in KR32 backend\n";
        break;
    }
}

}  // namespace krc::backend
