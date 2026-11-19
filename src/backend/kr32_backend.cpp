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
#include "backend/register_allocator.h"

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

// A 64-bit integer is wider than the KR32 word. Such a value occupies two
// consecutive frame words, low word first, and travels in the R0/R1 register
// pair, which plan.md §10.2 specifies for 64-bit arguments and returns.
bool is_wide(ir::TypePtr type) {
    return type && type->kind == ir::TypeKind::Integer && type->bits > 32;
}

std::string wide_helper_name(ir::Opcode opcode) {
    switch (opcode) {
    case ir::Opcode::UDiv:
        return "__krc_udivdi3";
    case ir::Opcode::SDiv:
        return "__krc_sdivdi3";
    case ir::Opcode::URem:
        return "__krc_umoddi3";
    case ir::Opcode::SRem:
        return "__krc_smoddi3";
    default:
        return {};
    }
}

const char* const kWideHelpers[] = {"__krc_udivdi3", "__krc_sdivdi3", "__krc_umoddi3",
                                    "__krc_smoddi3"};

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

// The KR32 ALU class takes its second operand either from the register named by
// bits 16..13 or from the 11-bit inline field, sign-extended. A constant in that
// field (-1024..1023) therefore need not be materialised into a register first;
// the bound is the assembler's own inline-immediate test
// (docs/targets/kr32.md: "inline 11-bit immediates with extension words").
bool fits_inline_literal(std::int64_t value) {
    return value >= -1024 && value <= 1023;
}

// Returns the literal an instruction can carry inline, or null when the operand
// has to reach it through a register. An out-of-range constant keeps the
// register form deliberately: an extension-word operand is a separate change
// with its own measurement, so this one covers the inline field only.
const ir::ConstantInt* inline_literal(const ir::Value* value) {
    if (!value || value->kind != ir::ValueKind::ConstantInt) {
        return nullptr;
    }
    const auto* constant = static_cast<const ir::ConstantInt*>(value);
    return fits_inline_literal(constant->value) ? constant : nullptr;
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

Kr32Backend::Kr32Backend(const ir::Module& module, const target::TargetProfile& profile,
                         OptimizationLevel level)
    : module_(module), profile_(profile), level_(level) {}

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

// A memory-class aggregate return is copied to a caller-owned destination
// supplied in R4, which does not consume an argument register:
// docs/abi.md, KR32.
bool Kr32Backend::is_memory_class_return(ir::TypePtr type) const {
    if (!type || (type->kind != ir::TypeKind::Struct && type->kind != ir::TypeKind::Union)) {
        return false;
    }
    std::string error;
    const auto size = size_of(type, error);
    return error.empty() && size > 8;
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
            std::size_t integer_arguments = 0;
            for (const auto& parameter : callee_type->parameters) {
                if (!parameter || ir::is_float(parameter)) {
                    continue;
                }
                if (parameter->kind == ir::TypeKind::Struct) {
                    std::string size_error;
                    const auto size = size_of(parameter, size_error);
                    if (size_error.empty()) {
                        integer_arguments += (size + 3) / 4;
                    }
                } else {
                    integer_arguments += is_wide(parameter) ? 2 : 1;
                }
            }
            const std::size_t fixed_stack_words =
                integer_arguments > 4 ? integer_arguments - 4 : 0;
            std::size_t outgoing = fixed_stack_words * 4;
            if (instruction->operands.size() > fixed + 1) {
                const auto extras = instruction->operands.size() - fixed - 1;
                outgoing = std::max<std::size_t>(outgoing,
                                                 fixed_stack_words * 4 +
                                                     extras * 8);
            }
            layout.outgoing_size = std::max<std::uint32_t>(
                layout.outgoing_size, static_cast<std::uint32_t>(outgoing));
        }
    }

    std::uint32_t offset = layout.outgoing_size;
    layout.float_temp = offset;
    offset += 8;
    if (is_memory_class_return(function.type ? function.type->return_type : nullptr)) {
        offset = static_cast<std::uint32_t>(round_up(offset, 4));
        layout.return_pointer = offset;
        layout.has_return_pointer = true;
        offset += 4;
    }
    const bool needs_eh_scratch = std::any_of(
        function.blocks.begin(), function.blocks.end(),
        [](const std::unique_ptr<ir::BasicBlock>& block) {
            return std::any_of(block->instructions.begin(), block->instructions.end(),
                               [](const std::unique_ptr<ir::Instruction>& instruction) {
                                   return instruction->opcode == ir::Opcode::LandingPad;
                               });
        });
    if (needs_eh_scratch) {
        offset = static_cast<std::uint32_t>(round_up(offset, 4));
        layout.eh_scratch = offset;
        layout.has_eh_scratch = true;
        offset += 8;
    }
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
    if (value && is_wide(value->type)) {
        if (reg != "R0") {
            out += "        HALT ; 64-bit value required in the R0/R1 pair\n";
            return;
        }
        load_wide(out, function, layout, value);
        return;
    }
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
    case ir::ValueKind::ConstantAddressArray:
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
    // An allocated value lives in a register for the whole of its block, so
    // reading it is a move rather than a frame access (ADR 0049).
    if (value->kind == ir::ValueKind::Instruction) {
        const auto allocated = allocated_registers_.find(value);
        if (allocated != allocated_registers_.end()) {
            const auto home = register_name(allocated->second);
            if (home != reg) {
                out += "        MOV " + std::string(reg) + ", " + home + "\n";
            }
            return;
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
    // An allocated value is kept in its register, so writing it is a move at
    // most, and nothing at all when the value is already there.
    const auto allocated = allocated_registers_.find(&instruction);
    if (allocated != allocated_registers_.end()) {
        const auto home = register_name(allocated->second);
        if (home != reg) {
            out += "        MOV " + home + ", " + std::string(reg) + "\n";
        }
        return;
    }
    if (is_wide(instruction.type)) {
        if (reg != "R0") {
            out += "        HALT ; 64-bit result required in the R0/R1 pair\n";
            return;
        }
        store_wide(out, function, layout, instruction);
        return;
    }
    std::string error;
    const auto slot = value_slot(function, layout, &instruction, error);
    if (!slot) {
        out += "; " + error + "\n";
        return;
    }
    out += "        STORE " + std::string(reg) + ", [R7 + " + std::to_string(*slot) + "]\n";
}

void Kr32Backend::load_wide(std::string& out, const ir::Function& function,
                            const FrameLayout& layout, const ir::Value* value) const {
    if (!value) {
        out += "        MOV R0, #0\n";
        out += "        MOV R1, #0\n";
        return;
    }
    if (value->kind == ir::ValueKind::ConstantInt) {
        const auto constant =
            static_cast<std::uint64_t>(static_cast<const ir::ConstantInt*>(value)->value);
        out += "        MOV R0, #" +
               std::to_string(static_cast<std::int32_t>(constant & 0xFFFFFFFFu)) + "\n";
        out += "        MOV R1, #" +
               std::to_string(static_cast<std::int32_t>(constant >> 32)) + "\n";
        return;
    }
    if (value->kind == ir::ValueKind::ConstantNull ||
        value->kind == ir::ValueKind::ConstantUndef ||
        value->kind == ir::ValueKind::ConstantFloat ||
        value->kind == ir::ValueKind::ConstantDataArray) {
        out += "        MOV R0, #0\n";
        out += "        MOV R1, #0\n";
        return;
    }
    if (value->kind == ir::ValueKind::Function || value->kind == ir::ValueKind::Global) {
        out += "        LEA R0, [" + value->name + "]\n";
        out += "        MOV R1, #0\n";
        return;
    }
    std::string error;
    const auto slot = value_slot(function, layout, value, error);
    if (!slot) {
        out += "; " + error + "\n";
        out += "        MOV R0, #0\n";
        out += "        MOV R1, #0\n";
        return;
    }
    out += "        LOAD R0, [R7 + " + std::to_string(*slot) + "]\n";
    out += "        LOAD R1, [R7 + " + std::to_string(*slot + 4) + "]\n";
}

void Kr32Backend::load_low_word(std::string& out, const ir::Function& function,
                                const FrameLayout& layout, const ir::Value* value,
                                std::string_view reg) const {
    if (!value || !is_wide(value->type)) {
        load_value(out, function, layout, value, reg);
        return;
    }
    // The low word alone addresses an object or decides a branch for every
    // value the KR32 address space can hold.
    if (value->kind == ir::ValueKind::ConstantInt) {
        const auto constant =
            static_cast<std::uint64_t>(static_cast<const ir::ConstantInt*>(value)->value);
        out += "        MOV " + std::string(reg) + ", #" +
               std::to_string(static_cast<std::int32_t>(constant & 0xFFFFFFFFu)) + "\n";
        return;
    }
    if (value->kind == ir::ValueKind::Function || value->kind == ir::ValueKind::Global) {
        out += "        LEA " + std::string(reg) + ", [" + value->name + "]\n";
        return;
    }
    if (value->kind == ir::ValueKind::ConstantNull ||
        value->kind == ir::ValueKind::ConstantUndef ||
        value->kind == ir::ValueKind::ConstantFloat ||
        value->kind == ir::ValueKind::ConstantDataArray) {
        out += "        MOV " + std::string(reg) + ", #0\n";
        return;
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

void Kr32Backend::store_wide(std::string& out, const ir::Function& function,
                             const FrameLayout& layout,
                             const ir::Instruction& instruction) const {
    std::string error;
    const auto slot = value_slot(function, layout, &instruction, error);
    if (!slot) {
        out += "; " + error + "\n";
        return;
    }
    out += "        STORE R0, [R7 + " + std::to_string(*slot) + "]\n";
    out += "        STORE R1, [R7 + " + std::to_string(*slot + 4) + "]\n";
}

// Lowers a 64-bit binary operation onto the 32-bit word pair. The pair is
// R0/R1 (low/high) for the first operand and R2/R3 for the second; R4..R6 are
// scratch. Division and remainder are runtime libcalls because the ISA has no
// 64-bit divide (plan.md §10.1, §14.7).
void Kr32Backend::emit_wide_binary(std::string& out, const ir::Function& function,
                                   const FrameLayout& layout,
                                   const ir::Instruction& instruction) const {
    load_wide(out, function, layout, instruction.operands[1]);
    out += "        MOV R2, R0\n";
    out += "        MOV R3, R1\n";
    load_wide(out, function, layout, instruction.operands[0]);

    const auto shift_labels = [&](const std::string& suffix) {
        const auto serial = wide_label_counter_++;
        const auto stem = ".L" + function.name + ".wide." + std::to_string(serial);
        return std::make_pair(stem + "." + suffix, stem + ".done");
    };

    switch (instruction.opcode) {
    case ir::Opcode::Add:
        out += "        ADD R0, R0, R2\n";
        out += "        SLTU R4, R0, R2\n";
        out += "        ADD R1, R1, R3\n";
        out += "        ADD R1, R1, R4\n";
        break;
    case ir::Opcode::Sub:
        out += "        SLTU R4, R0, R2\n";
        out += "        SUB R0, R0, R2\n";
        out += "        SUB R1, R1, R3\n";
        out += "        SUB R1, R1, R4\n";
        break;
    case ir::Opcode::Mul:
        out += "        MUL R4, R0, R3\n";
        out += "        MUL R5, R1, R2\n";
        out += "        MULHU R6, R0, R2\n";
        out += "        ADD R4, R4, R5\n";
        out += "        ADD R1, R4, R6\n";
        out += "        MUL R0, R0, R2\n";
        break;
    case ir::Opcode::And:
    case ir::Opcode::Or:
    case ir::Opcode::Xor: {
        const auto mnemonic = instruction.opcode == ir::Opcode::And
                                  ? "AND"
                                  : (instruction.opcode == ir::Opcode::Or ? "OR" : "XOR");
        out += "        " + std::string(mnemonic) + " R0, R0, R2\n";
        out += "        " + std::string(mnemonic) + " R1, R1, R3\n";
        break;
    }
    case ir::Opcode::Shl:
    case ir::Opcode::LShr:
    case ir::Opcode::AShr: {
        const auto labels = shift_labels("wide");
        out += "        CMP R2, #0\n";
        out += "        JZ " + labels.second + "\n";
        out += "        CMP R2, #32\n";
        out += "        JCS " + labels.first + "\n";
        // A count of 1..31 mixes both words; 32..63 keeps one word and fills
        // the other. The ISA masks shift counts to five bits, so the count of
        // zero and the count of 32 and above need their own paths.
        out += "        MOV R4, #32\n";
        out += "        SUB R4, R4, R2\n";
        if (instruction.opcode == ir::Opcode::Shl) {
            out += "        SHR R4, R0, R4\n";
            out += "        SHL R0, R0, R2\n";
            out += "        SHL R1, R1, R2\n";
            out += "        OR R1, R1, R4\n";
        } else if (instruction.opcode == ir::Opcode::LShr) {
            out += "        SHL R4, R1, R4\n";
            out += "        SHR R0, R0, R2\n";
            out += "        SHR R1, R1, R2\n";
            out += "        OR R0, R0, R4\n";
        } else {
            out += "        SHL R4, R1, R4\n";
            out += "        SHR R0, R0, R2\n";
            out += "        SAR R1, R1, R2\n";
            out += "        OR R0, R0, R4\n";
        }
        out += "        JMP " + labels.second + "\n";
        out += labels.first + ":\n";
        out += "        SUB R2, R2, #32\n";
        if (instruction.opcode == ir::Opcode::Shl) {
            out += "        SHL R1, R0, R2\n";
            out += "        MOV R0, #0\n";
        } else if (instruction.opcode == ir::Opcode::LShr) {
            out += "        SHR R0, R1, R2\n";
            out += "        MOV R1, #0\n";
        } else {
            out += "        SAR R0, R1, R2\n";
            out += "        SAR R1, R1, #31\n";
        }
        out += labels.second + ":\n";
        break;
    }
    case ir::Opcode::UDiv:
    case ir::Opcode::SDiv:
    case ir::Opcode::URem:
    case ir::Opcode::SRem:
        out += "        CALL " + wide_helper_name(instruction.opcode) + "\n";
        break;
    default:
        out += "        HALT ; unsupported 64-bit operation\n";
        break;
    }
    store_wide(out, function, layout, instruction);
}

// Compares the high words first; only an equal high word defers to the low
// word, which keeps the signedness of the predicate on both halves.
void Kr32Backend::emit_wide_icmp(std::string& out, const ir::Function& function,
                                 const FrameLayout& layout,
                                 const ir::Instruction& instruction) const {
    load_wide(out, function, layout, instruction.operands[1]);
    out += "        MOV R2, R0\n";
    out += "        MOV R3, R1\n";
    load_wide(out, function, layout, instruction.operands[0]);
    const auto serial = wide_label_counter_++;
    const auto stem = ".L" + function.name + ".icmp64." + std::to_string(serial);
    const auto true_label = stem + ".true";
    const auto low_label = stem + ".low";
    const auto done_label = stem + ".done";
    const auto condition = condition_for(instruction.icmp_predicate);
    out += "        MOV R4, #0\n";
    out += "        CMP R1, R3\n";
    out += "        JZ " + low_label + "\n";
    out += "        " + condition + " " + true_label + "\n";
    out += "        JMP " + done_label + "\n";
    out += low_label + ":\n";
    out += "        CMP R0, R2\n";
    out += "        " + condition + " " + true_label + "\n";
    out += "        JMP " + done_label + "\n";
    out += true_label + ":\n";
    out += "        MOV R4, #1\n";
    out += done_label + ":\n";
    store_value(out, function, layout, instruction, "R4");
}

std::vector<std::string> Kr32Backend::wide_division_helpers() const {
    std::unordered_set<std::string> used;
    for (const auto& function : module_.functions()) {
        for (const auto& block : function->blocks) {
            for (const auto& instruction : block->instructions) {
                if (!is_wide(instruction->type)) {
                    continue;
                }
                const auto helper = wide_helper_name(instruction->opcode);
                if (!helper.empty()) {
                    used.insert(helper);
                }
            }
        }
    }
    // Emit in the fixed declaration order so the assembly stays byte
    // identical across builds (§15.6).
    std::vector<std::string> helpers;
    for (const auto* helper : kWideHelpers) {
        if (used.contains(helper)) {
            helpers.emplace_back(helper);
        }
    }
    return helpers;
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

void Kr32Backend::emit_intrinsic(std::string& out, const ir::Function& function,
                                 const FrameLayout& layout,
                                 const ir::Instruction& instruction) const {
    // Each intrinsic takes record pointers and leaves a tile in T0. The tile
    // file is caller-saved, an intrinsic is an expression temporary, and the
    // sources are read before the destination is written, so a fixed T0-T3
    // assignment is enough and no tile allocation is required.
    const auto trace_argument = [&](std::size_t index, std::string_view reg) {
        load_value(out, function, layout, instruction.operands[index], reg);
    };
    const auto load_tile = [&](std::size_t index, std::string_view tile,
                               std::string_view opcode) {
        trace_argument(index, "R0");
        out += "        " + std::string(opcode) + " " + std::string(tile) + ", [R0]\n";
    };
    const auto store_result = [&](std::string_view tile, std::string_view opcode) {
        const auto slot = layout.slots.find(&instruction);
        if (slot == layout.slots.end()) {
            out += "        HALT ; intrinsic result has no frame slot\n";
            return;
        }
        out += "        LEA R1, [R7 + " + std::to_string(slot->second) + "]\n";
        out += "        " + std::string(opcode) + " " + std::string(tile) + ", [R1]\n";
    };

    switch (instruction.intrinsic) {
    case ir::Intrinsic::MatrixMultiply:
        load_tile(0, "T1", "MLOAD");
        load_tile(1, "T2", "MLOAD");
        out += "        MMUL T0, T1, T2\n";
        store_result("T0", "MSTORE");
        break;
    case ir::Intrinsic::MatrixMultiplyAdd:
        load_tile(2, "T0", "MLOAD");
        load_tile(0, "T1", "MLOAD");
        load_tile(1, "T2", "MLOAD");
        out += "        MMAC T0, T1, T2\n";
        store_result("T0", "MSTORE");
        break;
    case ir::Intrinsic::MatrixTranspose:
        load_tile(0, "T1", "MLOAD");
        out += "        MTRANS T0, T1\n";
        store_result("T0", "MSTORE");
        break;
    case ir::Intrinsic::MatrixVectorTransform:
        // MVEC leaves the product in column 0, so the result is transposed
        // back into the row-major vector the caller sees and stored as four
        // floats.
        load_tile(0, "T1", "MLOAD");
        load_tile(1, "T2", "VLOAD");
        out += "        MTRANS T3, T2\n";
        out += "        MVEC T0, T1, T3\n";
        out += "        MTRANS T3, T0\n";
        store_result("T3", "VSTORE");
        break;
    case ir::Intrinsic::VectorDot:
        load_tile(0, "T0", "MLOAD");
        load_tile(1, "T1", "VLOAD");
        out += "        VDOT F0, T0, T1\n";
        store_fp(out, function, layout, instruction, "F0");
        break;
    case ir::Intrinsic::TransformPush:
        load_tile(0, "T0", "MLOAD");
        out += "        MPUSH T0\n";
        break;
    case ir::Intrinsic::TransformPop:
        out += "        MPOP T0\n";
        trace_argument(0, "R1");
        out += "        MSTORE T0, [R1]\n";
        break;
    case ir::Intrinsic::TransformDrop:
        out += "        MDROP\n";
        break;
    case ir::Intrinsic::None:
        out += "        HALT ; unnamed intrinsic\n";
        break;
    }
}

std::uint32_t Kr32Backend::frame_bytes(const ir::Function& function) const {
    return layout_function(function).frame_size;
}

std::optional<std::string> Kr32Backend::emit(std::string& error) const {
    std::string out = "; KR32 assembly generated by krc\n";
    debug_info_.clear();
    debug_sources_.clear();
    debug_types_.clear();
    next_debug_source_ = 0;
    wide_label_counter_ = 0;

    for (const auto& helper : wide_division_helpers()) {
        out += "        .extern " + helper + "\n";
    }
    // Emitted only by a landing pad's resume, but declared unconditionally:
    // an unused .extern is harmless and keeps the call site simple.
    out += "        .extern _Unwind_Resume\n";

    for (const auto& global : module_.globals()) {
        if (global->external) {
            out += "        .extern " + global->name + "\n";
            continue;
        }
        out += "        .data\n";
        out += "        .align 4\n";
        out += global->name + ":\n";
        if (global->name.empty() || global->name.front() != '.') {
            if (global->weak) {
                out += "        .weak " + global->name + "\n";
            } else if (!global->internal) {
                out += "        .global " + global->name + "\n";
            }
        }
        if (global->initializer &&
            global->initializer->kind == ir::ValueKind::ConstantDataArray) {
            const auto* data = static_cast<const ir::ConstantDataArray*>(global->initializer);
            out += "        .byte " + escape_string(data->bytes) + "\n";
        } else if (global->initializer &&
                   global->initializer->kind == ir::ValueKind::ConstantAddressArray) {
            const auto* addresses =
                static_cast<const ir::ConstantAddressArray*>(global->initializer);
            for (const auto* element : addresses->elements) {
                if (element && (element->kind == ir::ValueKind::Function ||
                                element->kind == ir::ValueKind::Global)) {
                    out += "        .word " + element->name + "\n";
                } else {
                    out += "        .word 0\n";
                }
            }
        } else if (global->initializer &&
                   global->initializer->kind == ir::ValueKind::ConstantInt) {
            const auto constant = static_cast<std::uint64_t>(
                static_cast<const ir::ConstantInt*>(global->initializer)->value);
            out += "        .word " + std::to_string(static_cast<std::int32_t>(constant & 0xFFFFFFFFu)) +
                   "\n";
            if (is_wide(global->element_type)) {
                out += "        .word " +
                       std::to_string(static_cast<std::int32_t>(constant >> 32)) + "\n";
            }
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
        if (global->external) {
            continue;
        }
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
    // Identity removal is lowering, so it runs at every level; the `-O1` text
    // pass runs on top of it.
    auto text = peephole(out);
    if (level_ == OptimizationLevel::O1) {
        text = remove_redundant_jumps(text);
    }
    return text;
}

void Kr32Backend::emit_function(std::string& out, const ir::Function& function,
                                const FrameLayout& layout) const {
    compute_allocation(function);
    if (function.linkage == ir::Linkage::External) {
        out += "        .global " + function.name + "\n";
    } else if (function.linkage == ir::Linkage::Weak) {
        out += "        .weak " + function.name + "\n";
    }
    out += function.name + ":\n";
    out += "        PUSH R7\n";
    out += "        ADJSP #-" + std::to_string(layout.frame_size) + "\n";
    out += "        LEA R7, [SP]\n";
    if (layout.has_return_pointer) {
        out += "        STORE R4, [R7 + " + std::to_string(layout.return_pointer) + "]\n";
    }

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
        } else if (is_wide(parameter->type)) {
            for (std::uint64_t word = 0; word < 2; ++word) {
                const auto target = std::to_string(slot->second + word * 4);
                if (integer_parameter < 4) {
                    out += "        STORE " + register_name(integer_parameter) + ", [R7 + " +
                           target + "]\n";
                } else {
                    out += "        LOAD R0, [R7 + " +
                           std::to_string(layout.frame_size + 8 +
                                          (integer_parameter - 4) * 4) +
                           "]\n";
                    out += "        STORE R0, [R7 + " + target + "]\n";
                }
                integer_parameter++;
            }
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

    eh_regions_.clear();
    // A comparison whose only use is the conditional branch that tests it does
    // not need a boolean in a frame slot: the branch can consume the comparison
    // directly. That holds only while the comparison has exactly one use, so the
    // uses are counted for the whole function first.
    folded_conditions_.clear();
    {
        std::unordered_map<const ir::Value*, std::size_t> uses;
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block->instructions) {
                for (const auto* operand : instruction->operands) {
                    if (operand) {
                        ++uses[operand];
                    }
                }
            }
        }
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block->instructions) {
                if (instruction->opcode != ir::Opcode::CondBr ||
                    instruction->operands.empty()) {
                    continue;
                }
                const auto* condition =
                    dynamic_cast<const ir::Instruction*>(instruction->operands[0]);
                if (!condition || condition->opcode != ir::Opcode::ICmp ||
                    condition->operands.empty()) {
                    continue;
                }
                // A 64-bit comparison lowers into a sequence whose flags are not
                // the ones a conditional branch tests.
                if (is_wide(condition->operands[0]->type)) {
                    continue;
                }
                if (uses[condition] != 1) {
                    continue;
                }
                folded_conditions_.insert(condition);
            }
        }
    }
    for (const auto& block : function.blocks) {
        out += block_label(function, *block) + ":\n";
        for (const auto& instruction : block->instructions) {
            if (folded_conditions_.count(instruction.get()) != 0) {
                continue;
            }
            // A phi's value is written on the edge into its block, not here.
            if (instruction->opcode == ir::Opcode::Phi) {
                continue;
            }
            if (ir::is_terminator(instruction->opcode)) {
                emit_phi_copies(out, function, layout, *block, *instruction);
            }
            if (instruction->debug.valid()) {                auto source = debug_sources_.find(instruction->debug.file);
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

    // The display name is what a debugger shows; the symbol stays mangled
    // (docs/decisions/0030-debug-display-names.md).
    const auto& display_name =
        function.debug_name.empty() ? function.name : function.debug_name;
    debug_info_ += "F " + display_name + " " + function.name + " .L" + function.name +
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
            // A source variable is the alloca IRGen named `<variable>.addr`. A
            // compiler temporary (`call.temp`, `operator.temp`, `subscript.this`)
            // is not a variable a debugger should list, and a pass that promotes
            // one would leave its record pointing at a dead slot — so the record
            // is emitted for the named ones only (docs/optimize.md E4).
            auto name = instruction->name;
            if (name.size() <= 5 || name.compare(name.size() - 5, 5, ".addr") != 0) {
                continue;
            }
            name.resize(name.size() - 5);
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
    out += ".L" + function.name + ".end:\n";

    // The exception descriptor (ADR 0026). Every function gets one, even
    // without a protected call site, because the unwinder needs the frame size
    // of every frame it walks.
    out += "        .cpp_except\n";
    out += "        .align 4\n";
    out += "        .word 0x4845524B\n";
    out += "        .word " + function.name + "\n";
    out += "        .word .L" + function.name + ".end\n";
    out += "        .word " + std::to_string(layout.frame_size) + "\n";
    out += "        .word " + std::to_string(eh_regions_.size()) + "\n";
    for (const auto& region : eh_regions_) {
        out += "        .word " + region.start + "\n";
        out += "        .word " + region.end + "\n";
        out += "        .word " + region.pad + "\n";
    }
    out += "        .text\n";
}

void Kr32Backend::compute_allocation(const ir::Function& function) const {
    allocated_registers_.clear();
    if (level_ != OptimizationLevel::O1) {
        return;
    }
    // R0-R2 carry expression results and arguments, R4/R5 aggregates and
    // returns, R7 is the frame pointer. R8-R15 are untouched by the emitter, so
    // allocating one cannot collide with a sequence the backend writes by hand.
    std::vector<std::uint32_t> allowed;
    for (std::uint32_t index = 8; index <= 15; ++index) {
        allowed.push_back(index);
    }
    RegisterAllocation allocation;
    std::string error;
    if (!allocate_registers(function, allowed, allocation, error)) {
        return;
    }
    keep_local_allocations(function, allocation, allocated_registers_);
}


std::string Kr32Backend::result_register(const ir::Instruction& instruction) const {
    const auto found = allocated_registers_.find(&instruction);
    if (found == allocated_registers_.end()) {
        return "R0";
    }
    return register_name(found->second);
}

void Kr32Backend::emit_phi_copies(std::string& out, const ir::Function& function,
                                  const FrameLayout& layout, const ir::BasicBlock& block,
                                  const ir::Instruction& terminator) const {
    std::size_t successor_count = 0;
    for (const auto* successor : terminator.successors) {
        if (successor) {
            ++successor_count;
        }
    }
    for (const auto* successor : terminator.successors) {
        if (!successor) {
            continue;
        }
        std::size_t predecessor_count = 0;
        for (const auto& candidate : function.blocks) {
            if (!candidate) {
                continue;
            }
            for (const auto& other : candidate->instructions) {
                if (!other) {
                    continue;
                }
                for (const auto* target : other->successors) {
                    if (target == successor) {
                        ++predecessor_count;
                    }
                }
            }
        }
        // The copy belongs to this edge alone. When the source has another
        // successor and the destination another predecessor, this edge is shared
        // and there is nowhere to put the write.
        const bool critical = successor_count > 1 && predecessor_count > 1;
        for (const auto& instruction : successor->instructions) {
            if (!instruction || instruction->opcode != ir::Opcode::Phi) {
                continue;
            }
            for (const auto& incoming : instruction->incoming) {
                if (incoming.second != &block || !incoming.first) {
                    continue;
                }
                if (critical) {
                    out += "        HALT ; phi on a critical edge: split the edge first\n";
                    continue;
                }
                load_value(out, function, layout, incoming.first, "R0");
                store_value(out, function, layout, *instruction, "R0");
            }
        }
    }
}

void Kr32Backend::emit_instruction(std::string& out, const ir::Function& function,
                                   const FrameLayout& layout,
                                   const ir::Instruction& instruction) const {
    // The frame slot a pointer names directly, when the value is an alloca in
    // this function. Used to fold a load or store into an addressing mode.
    const auto frame_object =
        [&](const ir::Value* value) -> std::optional<std::uint32_t> {
        const auto* pointer = dynamic_cast<const ir::Instruction*>(value);
        if (!pointer || pointer->opcode != ir::Opcode::Alloca) {
            return std::nullopt;
        }
        const auto found = layout.alloca_offsets.find(pointer);
        if (found == layout.alloca_offsets.end()) {
            return std::nullopt;
        }
        return found->second;
    };
    const auto binary_instruction = [&](std::string_view mnemonic, bool commutative) {
        if (is_wide(instruction.operands[0]->type)) {
            emit_wide_binary(out, function, layout, instruction);
            return;
        }
        // A literal operand travels in the instruction rather than through a
        // register. Reversing the operands of a commutative op puts the literal
        // in that position; the value the instruction computes is unchanged.
        const auto* right_literal = inline_literal(instruction.operands[1]);
        const auto* left_literal =
            commutative ? inline_literal(instruction.operands[0]) : nullptr;
        const ir::Value* register_operand = right_literal ? instruction.operands[0]
                                            : left_literal ? instruction.operands[1]
                                                           : instruction.operands[0];
        const auto* literal = right_literal ? right_literal : left_literal;
        // Writing the allocated register directly is what saves a move; going
        // through R0 and copying afterwards leaves the count unchanged.
        const auto destination = result_register(instruction);
        load_value(out, function, layout, register_operand, "R0");
        if (literal) {
            out += "        " + std::string(mnemonic) + " " + destination + ", R0, #" +
                    std::to_string(literal->value) + "\n";
        } else {
            load_value(out, function, layout, instruction.operands[1], "R1");
            out += "        " + std::string(mnemonic) + " " + destination + ", R0, R1\n";
        }
        store_value(out, function, layout, instruction, destination);
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
        binary_instruction("ADD", true);
        break;
    case ir::Opcode::Sub:
        binary_instruction("SUB", false);
        break;
    case ir::Opcode::Mul:
        binary_instruction("MUL", true);
        break;
    case ir::Opcode::SDiv:
        binary_instruction("DIVS", false);
        break;
    case ir::Opcode::UDiv:
        binary_instruction("DIV", false);
        break;
    case ir::Opcode::SRem:
        binary_instruction("MODS", false);
        break;
    case ir::Opcode::URem:
        binary_instruction("MOD", false);
        break;
    case ir::Opcode::And:
        binary_instruction("AND", true);
        break;
    case ir::Opcode::Or:
        binary_instruction("OR", true);
        break;
    case ir::Opcode::Xor:
        binary_instruction("XOR", true);
        break;
    case ir::Opcode::Shl:
        binary_instruction("SHL", false);
        break;
    case ir::Opcode::LShr:
        binary_instruction("SHR", false);
        break;
    case ir::Opcode::AShr:
        binary_instruction("SAR", false);
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
        // A pointer that names a frame slot addresses it directly, so the load
        // is an addressing mode rather than a LEA followed by an indirect load.
        const bool scalar_result =
            !is_wide(instruction.type) && !ir::is_float(instruction.type) &&
            !(instruction.type && instruction.type->kind == ir::TypeKind::Struct);
        if (const auto object = frame_object(instruction.operands[0]);
            object && scalar_result) {
            const auto destination = result_register(instruction);
            out += "        " + load_opcode(instruction.type) + " " + destination +
                   ", [R7 + " + std::to_string(*object) + "]\n";
            store_value(out, function, layout, instruction, destination);
            break;
        }
        load_value(out, function, layout, instruction.operands[0], "R0");
        if (is_wide(instruction.type)) {
            out += "        LOAD R1, [R0 + 4]\n";
            out += "        LOAD R0, [R0]\n";
            store_value(out, function, layout, instruction, "R0");
        } else if (ir::is_float(instruction.type)) {
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
            // The address is in R0 and the result may go straight to its own
            // register, which is one instruction either way to compute and one
            // fewer to place.
            const auto destination = result_register(instruction);
            out += "        " + load_opcode(instruction.type) + " " + destination +
                   ", [R0]\n";
            store_value(out, function, layout, instruction, destination);
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
        } else if (is_wide(instruction.operands[0]->type)) {
            load_wide(out, function, layout, instruction.operands[0]);
            load_value(out, function, layout, instruction.operands[1], "R2");
            out += "        STORE R0, [R2]\n";
            out += "        STORE R1, [R2 + 4]\n";
        } else if (const auto object = frame_object(instruction.operands[1]); object) {
            // Storing through a frame slot is an addressing mode too.
            load_value(out, function, layout, instruction.operands[0], "R0");
            out += "        " + store_opcode(instruction.operands[0]->type) +
                   " R0, [R7 + " + std::to_string(*object) + "]\n";
        } else {
            load_value(out, function, layout, instruction.operands[0], "R0");
            load_value(out, function, layout, instruction.operands[1], "R1");
            out += "        " + store_opcode(instruction.operands[0]->type) + " R0, [R1]\n";
        }
        break;
    case ir::Opcode::GetElementPtr: {
        // The address is computed in the register the result belongs in, so an
        // allocated address costs no move at the end of the chain.
        const auto destination = result_register(instruction);
        load_value(out, function, layout, instruction.operands[0], destination);
        ir::TypePtr current = instruction.source_type;
        // A constant index or member offset is a byte displacement the address
        // register can take directly, so the offset accumulates here and leaves
        // as one ADD. The width is 32-bit because every emitted ADD and MUL
        // wraps at that width; unsigned arithmetic keeps the wrapping defined.
        std::uint64_t pending = 0;
        const auto flush_pending = [&]() {
            if (pending == 0) {
                return;
            }
            out += "        ADD " + destination + ", " + destination + ", #" +
                   std::to_string(static_cast<std::int32_t>(
                       static_cast<std::uint32_t>(pending))) + "\n";
            pending = 0;
        };
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
                pending += offset;
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
            if (const auto* constant =
                    dynamic_cast<const ir::ConstantInt*>(instruction.operands[index])) {
                pending += static_cast<std::uint64_t>(constant->value) * stride;
                continue;
            }
            flush_pending();
            load_low_word(out, function, layout, instruction.operands[index], "R1");
            if (stride != 1) {
                out += "        MUL R1, R1, #" + std::to_string(stride) + "\n";
            }
            out += "        ADD " + destination + ", " + destination + ", R1\n";
        }
        flush_pending();
        store_value(out, function, layout, instruction, destination);
        break;
    }
    case ir::Opcode::ICmp: {
        if (is_wide(instruction.operands[0]->type)) {
            emit_wide_icmp(out, function, layout, instruction);
            break;
        }
        load_value(out, function, layout, instruction.operands[0], "R0");
        if (const auto* literal = inline_literal(instruction.operands[1])) {
            out += "        CMP R0, #" + std::to_string(literal->value) + "\n";
        } else {
            load_value(out, function, layout, instruction.operands[1], "R1");
            out += "        CMP R0, R1\n";
        }
        out += "        MOV R2, #0\n";
        const auto serial = compare_label_counter_++;
        const auto true_label = ".L" + function.name + ".icmp." +
                                std::to_string(serial) + ".true";
        const auto done_label = ".L" + function.name + ".icmp." +
                                std::to_string(serial) + ".done";
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
        if (is_wide(instruction.operands[0]->type)) {
            load_wide(out, function, layout, instruction.operands[0]);
            out += "        OR R0, R0, R1\n";
            out += "        CMP R0, #0\n";
            out += "        JNZ " + block_label(function, *instruction.successors[0]) + "\n";
            out += "        JMP " + block_label(function, *instruction.successors[1]) + "\n";
            break;
        }
        if (const auto* condition =
                dynamic_cast<const ir::Instruction*>(instruction.operands[0]);
            condition && folded_conditions_.count(condition) != 0) {
            load_value(out, function, layout, condition->operands[0], "R0");
            if (const auto* literal = inline_literal(condition->operands[1])) {
                out += "        CMP R0, #" + std::to_string(literal->value) + "\n";
            } else {
                load_value(out, function, layout, condition->operands[1], "R1");
                out += "        CMP R0, R1\n";
            }
            out += "        " + condition_for(condition->icmp_predicate) + " " +
                   block_label(function, *instruction.successors[0]) + "\n";
            out += "        JMP " + block_label(function, *instruction.successors[1]) + "\n";
            break;
        }
        load_value(out, function, layout, instruction.operands[0], "R0");
        out += "        CMP R0, #0\n";
        out += "        JNZ " + block_label(function, *instruction.successors[0]) + "\n";
        out += "        JMP " + block_label(function, *instruction.successors[1]) + "\n";
        break;
    case ir::Opcode::Ret:
        if (!instruction.operands.empty()) {
            if (ir::is_float(instruction.operands[0]->type)) {
                load_fp(out, function, layout, instruction.operands[0], "F0");
            } else if (instruction.operands[0]->type &&
                       instruction.operands[0]->type->kind ==
                           ir::TypeKind::Struct) {
                const auto* value = instruction.operands[0];
                const auto slot = layout.slots.find(value);
                std::string size_error;
                const auto size = size_of(value->type, size_error);
                if (!size_error.empty() || slot == layout.slots.end()) {
                    out += "        ; aggregate return unavailable\n";
                } else if (is_memory_class_return(value->type)) {
                    if (!layout.has_return_pointer) {
                        out += "        HALT ; memory-class return has no destination\n";
                    } else {
                        out += "        LOAD R1, [R7 + " +
                               std::to_string(layout.return_pointer) + "]\n";
                        for (std::uint64_t return_offset = 0; return_offset < size;
                             return_offset += 4) {
                            out += "        LOAD R0, [R7 + " +
                                   std::to_string(slot->second + return_offset) + "]\n";
                            out += "        STORE R0, [R1 + " +
                                   std::to_string(return_offset) + "]\n";
                        }
                    }
                } else {
                    const auto words = (size + 3) / 4;
                    if (words > 2) {
                        out += "        ; aggregate return unavailable\n";
                    } else {
                        for (std::uint64_t word = 0; word < words; ++word) {
                            out += "        LOAD " +
                                   std::string(register_name(word)) + ", [R7 + " +
                                   std::to_string(slot->second + word * 4) + "]\n";
                        }
                    }
                }
            } else {
                load_value(out, function, layout, instruction.operands[0], "R0");
            }
        }
        out += "        JMP .L" + function.name + ".ret\n";
        break;
    case ir::Opcode::Call:
    case ir::Opcode::Invoke: {
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
        std::size_t fixed_integer_arguments = 0;
        for (const auto& parameter : callee_type->parameters) {
            if (!parameter || ir::is_float(parameter)) {
                continue;
            }
            if (parameter->kind == ir::TypeKind::Struct) {
                std::string size_error;
                const auto size = size_of(parameter, size_error);
                if (size_error.empty()) {
                    fixed_integer_arguments += (size + 3) / 4;
                }
            } else {
                fixed_integer_arguments += is_wide(parameter) ? 2 : 1;
            }
        }
        const std::size_t fixed_stack_words =
            fixed_integer_arguments > 4 ? fixed_integer_arguments - 4 : 0;
        for (std::size_t index = fixed; index < total; ++index) {
            const auto* argument = instruction.operands[index + 1];
            const auto offset = static_cast<std::uint32_t>(
                fixed_stack_words * 4 + (index - fixed) * 8);
            if (argument->type && ir::is_float(argument->type)) {
                load_fp(out, function, layout, argument, "F0");
                out += "        FSTORE.D F0, [R7 + " + std::to_string(offset) + "]\n";
            } else if (argument->type && is_wide(argument->type)) {
                load_wide(out, function, layout, argument);
                out += "        STORE R0, [R7 + " + std::to_string(offset) + "]\n";
                out += "        STORE R1, [R7 + " + std::to_string(offset + 4) + "]\n";
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
                load_fp(out, function, layout, argument,
                        "F" + std::to_string(fp_index++));
            }
        }
        for (std::size_t index = 0; index < fixed; ++index) {
            const auto* argument = instruction.operands[index + 1];
            if (argument->type && ir::is_float(argument->type)) {
                continue;
            }
            if (argument->type &&
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
            } else if (argument->type && is_wide(argument->type)) {
                // Each word of a 64-bit argument consumes one argument word.
                // The pair is moved a word at a time so that the argument
                // registers already holding earlier arguments survive.
                const auto slot = layout.slots.find(argument);
                const bool constant_argument = argument->kind == ir::ValueKind::ConstantInt;
                const auto constant = constant_argument
                                          ? static_cast<std::uint64_t>(
                                                static_cast<const ir::ConstantInt*>(argument)->value)
                                          : 0;
                for (std::uint64_t word = 0; word < 2; ++word) {
                    const auto position = integer_index + word;
                    const auto half = static_cast<std::int32_t>(
                        word == 0 ? (constant & 0xFFFFFFFFu) : (constant >> 32));
                    if (position < 4) {
                        const auto target = register_name(position);
                        if (constant_argument) {
                            out += "        MOV " + target + ", #" + std::to_string(half) + "\n";
                        } else if (slot != layout.slots.end()) {
                            out += "        LOAD " + target + ", [R7 + " +
                                   std::to_string(slot->second + word * 4) + "]\n";
                        } else {
                            out += "        HALT ; 64-bit argument has no frame slot\n";
                        }
                    } else {
                        if (constant_argument) {
                            out += "        MOV R4, #" + std::to_string(half) + "\n";
                        } else if (slot != layout.slots.end()) {
                            out += "        LOAD R4, [R7 + " +
                                   std::to_string(slot->second + word * 4) + "]\n";
                        } else {
                            out += "        HALT ; 64-bit argument has no frame slot\n";
                        }
                        out += "        STORE R4, [R7 + " +
                               std::to_string((position - 4) * 4) + "]\n";
                    }
                }
                integer_index += 2;
            } else {
                if (integer_index < 4) {
                    load_value(out, function, layout, argument,
                               register_name(integer_index++));
                } else {
                    load_value(out, function, layout, argument, "R4");
                    out += "        STORE R4, [R7 + " +
                           std::to_string((integer_index - 4) * 4) + "]\n";
                    integer_index++;
                }
            }
        }
        bool memory_return = false;
        std::uint32_t return_destination = 0;
        if (instruction.type && instruction.type->kind == ir::TypeKind::Struct &&
            is_memory_class_return(instruction.type)) {
            const auto slot = layout.slots.find(&instruction);
            if (slot != layout.slots.end()) {
                memory_return = true;
                return_destination = slot->second;
            }
        }
        // An invoke records the call's address and the return address it
        // pushes, so the unwinder can find the pad for a return address.
        std::string region_start;
        if (instruction.opcode == ir::Opcode::Invoke) {
            region_start = ".Linvoke." + function.name + "." +
                           std::to_string(eh_regions_.size());
            out += "        " + region_start + ":\n";
        }
        if (instruction.operands[0]->kind == ir::ValueKind::Function) {
            if (memory_return) {
                out += "        LEA R4, [R7 + " + std::to_string(return_destination) +
                       "]\n";
            }
            out += "        CALL " + instruction.operands[0]->name + "\n";
        } else {
            load_value(out, function, layout, instruction.operands[0], "R5");
            if (memory_return) {
                out += "        LEA R4, [R7 + " + std::to_string(return_destination) +
                       "]\n";
            }
            out += "        CALL R5\n";
        }
        if (instruction.opcode == ir::Opcode::Invoke) {
            const auto region_end = region_start + ".end";
            out += "        " + region_end + ":\n";
            if (instruction.successors.size() >= 2 && instruction.successors[1]) {
                eh_regions_.push_back(
                    {region_start, region_end,
                     block_label(function, *instruction.successors[1])});
            }
        }
        if (!memory_return && instruction.type && !ir::is_void(instruction.type)) {
            if (ir::is_float(instruction.type)) {
                store_fp(out, function, layout, instruction, "F0");
            } else if (instruction.type->kind == ir::TypeKind::Struct) {
                const auto slot = layout.slots.find(&instruction);
                std::string size_error;
                const auto size = size_of(instruction.type, size_error);
                const auto words = (size + 3) / 4;
                if (!size_error.empty() || slot == layout.slots.end() ||
                    words > 2) {
                    out += "        ; aggregate result unavailable\n";
                } else {
                    for (std::uint64_t word = 0; word < words; ++word) {
                        out += "        STORE " +
                               std::string(register_name(word)) + ", [R7 + " +
                               std::to_string(slot->second + word * 4) + "]\n";
                    }
                }
            } else {
                store_value(out, function, layout, instruction, "R0");
            }
        }
        if (instruction.opcode == ir::Opcode::Invoke &&
            !instruction.successors.empty() && instruction.successors[0]) {
            out += "        JMP " +
                   block_label(function, *instruction.successors[0]) + "\n";
        }
        break;
    }
    case ir::Opcode::LandingPad:
        if (!layout.has_eh_scratch) {
            out += "        HALT ; landing pad has no frame scratch\n";
            break;
        }
        // The unwinder enters here with the exception object pointer in R0 and
        // its typeinfo in R1 (ADR 0026).
        out += "        STORE R0, [R7 + " + std::to_string(layout.eh_scratch) + "]\n";
        out += "        STORE R1, [R7 + " +
               std::to_string(layout.eh_scratch + 4) + "]\n";
        out += "        LEA R0, [R7 + " + std::to_string(layout.eh_scratch) + "]\n";
        store_value(out, function, layout, instruction, "R0");
        break;
    case ir::Opcode::Resume:
        load_value(out, function, layout, instruction.operands[0], "R0");
        out += "        CALL _Unwind_Resume\n";
        out += "        HALT ; _Unwind_Resume returned\n";
        break;
    case ir::Opcode::Intrinsic:
        emit_intrinsic(out, function, layout, instruction);
        break;
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
        if (is_wide(instruction.type)) {
            // The ISA converts to a 32-bit word. A wider destination extends
            // that word, so values outside the 32-bit range still convert
            // wrongly, but the high word is defined rather than stale.
            if (instruction.opcode == ir::Opcode::FPToSI) {
                out += "        MOV R1, R0\n";
                out += "        SAR R1, R1, #31\n";
            } else {
                out += "        MOV R1, #0\n";
            }
        }
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
        const auto source_type = instruction.operands[0]->type;
        const auto source_bits =
            source_type && source_type->bits != 0 ? source_type->bits : 32;
        const auto dest_bits =
            instruction.type && instruction.type->bits != 0 ? instruction.type->bits : 32;
        const bool sign_extend = instruction.opcode == ir::Opcode::SExt;
        const auto extend_from = [&](unsigned from_bits) {
            if (sign_extend && from_bits < 32) {
                out += "        SHL R0, R0, #" + std::to_string(32 - from_bits) + "\n";
                out += "        SAR R0, R0, #" + std::to_string(32 - from_bits) + "\n";
            } else if (!sign_extend && instruction.opcode == ir::Opcode::ZExt &&
                       from_bits < 32) {
                out += "        AND R0, R0, #" +
                       std::to_string((1ull << from_bits) - 1) + "\n";
            }
        };
        if (is_wide(instruction.type)) {
            if (is_wide(source_type)) {
                load_wide(out, function, layout, instruction.operands[0]);
            } else {
                load_value(out, function, layout, instruction.operands[0], "R0");
                extend_from(source_bits);
                // The high word is all sign bits or zero, so the 32-bit width
                // is the word width either way.
                if (sign_extend) {
                    out += "        MOV R1, R0\n";
                    out += "        SAR R1, R1, #31\n";
                } else {
                    out += "        MOV R1, #0\n";
                }
            }
            store_value(out, function, layout, instruction, "R0");
            break;
        }
        if (is_wide(source_type)) {
            // The low word of a 64-bit value is its truncation.
            load_wide(out, function, layout, instruction.operands[0]);
        } else {
            load_value(out, function, layout, instruction.operands[0], "R0");
            extend_from(source_bits);
        }
        if (dest_bits < 32) {
            out += "        AND R0, R0, #" + std::to_string((1ull << dest_bits) - 1) + "\n";
        }
        store_value(out, function, layout, instruction, "R0");
        break;
    }
    case ir::Opcode::BitCast:
    case ir::Opcode::PtrToInt:
    case ir::Opcode::IntToPtr:
        if (is_wide(instruction.operands[0]->type)) {
            // A cast between equal-width values is a bit copy, so both words
            // travel; a narrower destination stores only the low one. Loading
            // the low word alone here truncated an `i64` to its low half.
            load_wide(out, function, layout, instruction.operands[0]);
        } else {
            load_low_word(out, function, layout, instruction.operands[0], "R0");
            if (is_wide(instruction.type)) {
                // A 32-bit source zero-extends into the pair; a pointer fits
                // the KR32 address space, so its high word is zero.
                out += "        MOV R1, #0\n";
            }
        }
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
