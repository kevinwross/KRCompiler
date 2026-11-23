#include "kr232_backend.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace krc::backend {
namespace {

// The ABI's frame is a multiple of the 4-byte stack alignment.
std::uint32_t align_frame(std::uint32_t bytes) { return (bytes + 3u) & ~3u; }

// The alignment a type requires. A packed record aligns to one byte, which is
// what makes a three-char record three bytes rather than twelve -- the ABI's
// size-exact copy depends on it.
std::uint32_t type_bytes(const ir::Type* type);

std::uint32_t type_align(const ir::Type* type) {
    if (type == nullptr) {
        return 1;
    }
    switch (type->kind) {
        case ir::TypeKind::Void:
            return 1;
        case ir::TypeKind::Pointer:
            return 4;
        case ir::TypeKind::Integer:
        case ir::TypeKind::Float: {
            const std::uint32_t bytes = (type->bits + 7u) / 8u;
            return bytes < 4u ? (bytes == 0 ? 1u : bytes) : 4u;
        }
        case ir::TypeKind::Array:
            return type_align(type->element);
        case ir::TypeKind::Struct:
        case ir::TypeKind::Union: {
            if (type->packed) {
                return 1;
            }
            std::uint32_t alignment = 1;
            for (const ir::Type* field : type->fields) {
                const std::uint32_t field_align = type_align(field);
                if (field_align > alignment) {
                    alignment = field_align;
                }
            }
            return alignment;
        }
        case ir::TypeKind::Function:
            return 1;
    }
    return 1;
}

// kr232 sizes: a pointer is 32 bits, an integer or float is `bits` wide, an array
// is its element times its count, a record is its fields laid out at their natural
// offsets. `type_size` is private to each backend, so this backend owns its own.
std::uint32_t type_bytes(const ir::Type* type) {
    if (type == nullptr) {
        return 0;
    }
    switch (type->kind) {
        case ir::TypeKind::Void:
            return 0;
        case ir::TypeKind::Pointer:
            return 4;
        case ir::TypeKind::Integer:
        case ir::TypeKind::Float:
            return (type->bits + 7u) / 8u;
        case ir::TypeKind::Array:
            return type_bytes(type->element) * static_cast<std::uint32_t>(type->count);
        case ir::TypeKind::Struct:
        case ir::TypeKind::Union: {
            // Fields take their natural offsets; a packed record pads nothing. A
            // three-char record is three bytes, which the ABI's size-exact copy
            // depends on.
            std::uint32_t bytes = 0;
            for (const ir::Type* field : type->fields) {
                const std::uint32_t field_align = type->packed ? 1u : type_align(field);
                bytes = (bytes + field_align - 1u) & ~(field_align - 1u);
                bytes += type_bytes(field);
            }
            const std::uint32_t record_align = type->packed ? 1u : type_align(type);
            return (bytes + record_align - 1u) & ~(record_align - 1u);
        }
        case ir::TypeKind::Function:
            return 0;
    }
    return 0;
}

// The byte offset of a record's field, which is what a member index means.
std::uint32_t field_offset(const ir::Type* record, std::size_t index) {
    std::uint32_t bytes = 0;
    for (std::size_t i = 0; i < record->fields.size(); ++i) {
        const ir::Type* field = record->fields[i];
        const std::uint32_t field_align = record->packed ? 1u : type_align(field);
        bytes = (bytes + field_align - 1u) & ~(field_align - 1u);
        if (i == index) {
            return bytes;
        }
        bytes += type_bytes(field);
    }
    return bytes;
}

// R6 is the value scratch and R8 the address scratch. They are separate on
// purpose: materialising an address is `ld`, which overwrites its register, and a
// shared pool let that overwrite a value four times.
constexpr unsigned kValueReg = 6;
constexpr unsigned kSecondReg = 7;
constexpr unsigned kAddressReg = 8;

}  // namespace

Kr232Backend::Kr232Backend(const ir::Module& module, const target::TargetProfile& profile,
                           OptimizationLevel level)
    : module_(module), profile_(profile), level_(level) {}

std::uint32_t Kr232Backend::frame_bytes(const ir::Function& function) const {
    // Every local and every value that must survive to a later instruction gets a
    // slot. Spilling everything is slower than a register allocator and it is
    // correct, which is the trade this backend makes until one exists.
    std::uint32_t slots = 0;
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block->instructions) {
            if (instruction->opcode == ir::Opcode::Alloca) {
                slots += type_bytes(instruction->allocated_type);
            } else if (instruction->type != nullptr &&
                       instruction->type->kind != ir::TypeKind::Void) {
                slots += 4;
            }
        }
    }
    slots += static_cast<std::uint32_t>(function.parameters.size()) * 4u;
    return align_frame(slots);
}

std::optional<std::string> Kr232Backend::emit(std::string& error) const {
    debug_info_.clear();
    std::ostringstream out;
    out << "; kr232 backend, optimization level " << static_cast<int>(level_) << "\n";
    // The floating-point constants this module uses, each one an eight-byte entry
    // in `.data`; a constant is loaded with `fld` from its address.
    std::vector<double> fp_constants;
    for (const auto& function : module_.functions()) {
        for (const auto& block : function->blocks) {
            for (const auto& instruction : block->instructions) {
                for (const ir::Value* operand : instruction->operands) {
                    const auto* constant = dynamic_cast<const ir::ConstantFloat*>(operand);
                    if (constant == nullptr) {
                        continue;
                    }
                    if (std::find(fp_constants.begin(), fp_constants.end(), constant->value) ==
                        fp_constants.end()) {
                        fp_constants.push_back(constant->value);
                    }
                }
            }
        }
    }
    if (!fp_constants.empty()) {
        out << ".data\n";
        for (const double value : fp_constants) {
            std::ostringstream label;
            label << ".Lfp" << std::hex << std::hash<double>{}(value);
            out << label.str() << ":\n";
            std::uint64_t bits = 0;
            std::memcpy(&bits, &value, sizeof bits);
            out << "  .word " << (bits & 0xFFFFFFFFu) << "\n";
            out << "  .word " << ((bits >> 32) & 0xFFFFFFFFu) << "\n";
        }
    }
    if (!module_.globals().empty()) {
        out << ".data\n";
        for (const auto& global : module_.globals()) {
            if (global->external) {
                continue;
            }
            out << global->name << ":\n";
            // A table of addresses is what a vtable is: one relocated word per
            // element, naming the function or object it points at.
            if (const auto* addresses =
                    dynamic_cast<const ir::ConstantAddressArray*>(global->initializer)) {
                for (const ir::Value* element : addresses->elements) {
                    // kr232 reserves the word below the vptr for type information
                    // and requires it to be zero, unlike the Itanium ABI's pointer
                    // to a typeinfo object. The IR names that slot with a `_ZTI`
                    // symbol, so its word is emitted as zero.
                    if (const auto* named = dynamic_cast<const ir::GlobalVariable*>(element)) {
                        if (named->name.rfind("_ZTI", 0) == 0) {
                            out << "  .word 0\n";
                            continue;
                        }
                    }
                    if (const auto* function = dynamic_cast<const ir::Function*>(element)) {
                        out << "  .word " << function->name << "\n";
                    } else if (const auto* target =
                                   dynamic_cast<const ir::GlobalVariable*>(element)) {
                        out << "  .word " << target->name << "\n";
                    } else {
                        out << "  .word 0\n";
                    }
                }
                continue;
            }
            const auto* bytes = dynamic_cast<const ir::ConstantDataArray*>(global->initializer);
            if (bytes != nullptr && !bytes->bytes.empty()) {
                for (const std::uint8_t byte : bytes->bytes) {
                    out << "  .byte " << static_cast<unsigned>(byte) << "\n";
                }
            } else {
                out << "  .word 0\n";
            }
        }
    }

    for (const auto& function : module_.functions()) {
        if (function->declaration) {
            continue;
        }
        const std::string name = function->name;
        out << ".globl " << name << "\n";
        out << ".text\n";
        out << name << ":\n";

        // Slots: allocas first (their address is FP-relative), then one slot per
        // value, then one per parameter. An f64 takes two slots and starts on an
        // eight-byte boundary, so adjacent doubles cannot overlap.
        const auto slots_for = [](const ir::Type* type) -> std::uint32_t {
            return type != nullptr && type->kind == ir::TypeKind::Float &&
                           (type->bits + 7u) / 8u >= 8u
                       ? 2u
                       : 1u;
        };
        std::unordered_map<const ir::Value*, std::uint32_t> slot_of;
        std::uint32_t next_slot = 0;
        for (const auto& block : function->blocks) {
            for (const auto& instruction : block->instructions) {
                if (instruction->opcode == ir::Opcode::Alloca) {
                    slot_of.emplace(instruction.get(), next_slot);
                    next_slot += align_frame(type_bytes(instruction->allocated_type)) / 4u;
                }
            }
        }
        for (const auto& block : function->blocks) {
            for (const auto& instruction : block->instructions) {
                if (instruction->opcode != ir::Opcode::Alloca && instruction->type != nullptr &&
                    instruction->type->kind != ir::TypeKind::Void) {
                    const std::uint32_t slots = slots_for(instruction->type);
                    if (slots == 2u && (next_slot % 2u) != 0u) {
                        ++next_slot;
                    }
                    slot_of.emplace(instruction.get(), next_slot);
                    next_slot += slots;
                }
            }
        }
        // A call that returns a record needs somewhere to put it: the callee copies
        // into a destination the caller names. Those destinations get their own
        // frame space, recorded per call.
        std::unordered_map<const ir::Value*, std::uint32_t> aggregate_destination;
        for (const auto& block : function->blocks) {
            for (const auto& instruction : block->instructions) {
                if (instruction->opcode == ir::Opcode::Call && instruction->type != nullptr &&
                    (instruction->type->kind == ir::TypeKind::Struct ||
                     instruction->type->kind == ir::TypeKind::Union ||
                     instruction->type->kind == ir::TypeKind::Array)) {
                    aggregate_destination.emplace(instruction.get(), next_slot);
                    next_slot += align_frame(type_bytes(instruction->type)) / 4u;
                }
            }
        }
        // A function that returns a record receives its destination in R6.
        const bool returns_record = function->type != nullptr &&
                                    function->type->return_type != nullptr &&
                                    function->type->return_type->kind != ir::TypeKind::Void &&
                                    (function->type->return_type->kind == ir::TypeKind::Struct ||
                                     function->type->return_type->kind == ir::TypeKind::Union ||
                                     function->type->return_type->kind == ir::TypeKind::Array);
        std::uint32_t return_pointer_slot = 0;
        if (returns_record) {
            return_pointer_slot = next_slot++;
        }
        for (const auto& parameter : function->parameters) {
            const std::uint32_t slots = slots_for(parameter->type);
            if (slots == 2u && (next_slot % 2u) != 0u) {
                ++next_slot;
            }
            slot_of.emplace(parameter.get(), next_slot);
            next_slot += slots;
        }
        const std::uint32_t frame = align_frame(next_slot * 4u);

        out << "  enter #" << frame << ", #0\n";
        if (returns_record) {
            out << "  st [r13-" << (4u + 4u * return_pointer_slot) << "], r6\n";
        }
        // Arguments arrive in R2-R5, except floating-point ones, which arrive in
        // F0-F3 and are spilled to their slots with `fst`.
        std::size_t integer_argument = 0;
        std::size_t float_argument = 0;
        for (std::size_t i = 0; i < function->parameters.size(); ++i) {
            const auto slot = slot_of.at(function->parameters[i].get());
            const ir::Type* type = function->parameters[i]->type;
            const bool is_float = type != nullptr && type->kind == ir::TypeKind::Float;
            if (is_float) {
                out << "  fst f" << float_argument++ << ", [r13-" << (4u + 4u * slot) << "]\n";
            } else {
                out << "  st [r13-" << (4u + 4u * slot) << "], r" << (2u + integer_argument++)
                    << "\n";
            }
        }

        std::uint32_t next_compare = 0;
        std::uint32_t next_indirect = 0;
        std::unordered_map<const ir::BasicBlock*, std::size_t> block_index;
        for (std::size_t i = 0; i < function->blocks.size(); ++i) {
            block_index.emplace(function->blocks[i].get(), i);
        }
        const auto slot_address = [&](std::uint32_t slot) -> std::string {
            // A d8 displacement is a signed byte; a deeper slot is reached through R8.
            const std::uint32_t frame_offset = 4u + 4u * slot;
            // A d8 displacement is a signed byte, so a frame deeper than that
            // cannot be expressed yet; the assembler will refuse it by name
            // rather than the backend emitting code it cannot encode.

            return "[r13-" + std::to_string(frame_offset) + "]";
        };
        // A frame access that names its operand after any prologue it needs, so a
        // deep slot is reached through R8 instead of an unencodable displacement.
        const auto frame_load = [&](const char* opcode, std::uint32_t slot,
                                    const std::string& target) {
            const std::uint32_t offset = 4u + 4u * slot;
            if (offset > 128u) {
                out << "  mov r8, r13\n";
                out << "  sub r8, #" << offset << "\n";
                out << "  " << opcode << " " << target << ", [r8]\n";
                return;
            }
            out << "  " << opcode << " " << target << ", [r13-" << offset << "]\n";
        };
        const auto frame_store = [&](const char* opcode, std::uint32_t slot,
                                     const std::string& source) {
            const std::uint32_t offset = 4u + 4u * slot;
            if (offset > 128u) {
                out << "  mov r8, r13\n";
                out << "  sub r8, #" << offset << "\n";
                out << "  " << opcode << " [r8], " << source << "\n";
                return;
            }
            out << "  " << opcode << " [r13-" << offset << "], " << source << "\n";
        };
        const auto materialize = [&](const ir::Value* value, unsigned reg) {
            if (value == nullptr) {
                out << "  ld r" << reg << ", #0\n";
                return;
            }
            if (const auto* constant = dynamic_cast<const ir::ConstantInt*>(value)) {
                out << "  ld r" << reg << ", #" << constant->value << "\n";
                return;
            }
            if (const auto* global = dynamic_cast<const ir::GlobalVariable*>(value)) {
                out << "  ld r" << reg << ", #" << global->name << "\n";
                return;
            }
            // An alloca's value is its address, which is FP-relative. A cast or a
            // member GEP built on an alloca reaches here, and reading a slot would
            // hand the caller the alloca's contents instead of its address.
            if (const auto* instruction = dynamic_cast<const ir::Instruction*>(value)) {
                if (instruction->opcode == ir::Opcode::Alloca) {
                    const auto alloca_slot = slot_of.find(value);
                    if (alloca_slot != slot_of.end()) {
                        out << "  mov r" << reg << ", r13\n";
                        out << "  sub r" << reg << ", #" << (4u + 4u * alloca_slot->second)
                            << "\n";
                        return;
                    }
                }
            }
            const auto slot = slot_of.find(value);
            if (slot == slot_of.end()) {
                out << "  ld r" << reg << ", #0\n";
                return;
            }
            frame_load("ld", slot->second, "r" + std::to_string(reg));
        };
        // A load or store address: FP-relative for an alloca, R8 for a computed
        // pointer. Naming R8 keeps the address out of the value register.
        const auto address_of = [&](const ir::Value* value) -> std::string {
            if (const auto* instruction = dynamic_cast<const ir::Instruction*>(value)) {
                if (instruction->opcode == ir::Opcode::Alloca) {
                    const auto slot = slot_of.find(value);
                    if (slot != slot_of.end()) {
                        return slot_address(slot->second);
                    }
                }
            }
            materialize(value, kAddressReg);
            return "[r8]";
        };
        // A floating-point value is two words in a slot; a constant is loaded from
        // its own .data entry, emitted in `fp_constants`.
        const auto materialize_float = [&](const ir::Value* value, unsigned fregister) {
            if (const auto* constant = dynamic_cast<const ir::ConstantFloat*>(value)) {
                std::ostringstream label;
                label << ".Lfp" << std::hex << std::hash<double>{}(constant->value);
                out << "  ld r" << kAddressReg << ", #" << label.str() << "\n";
                out << "  fld f" << fregister << ", [r" << kAddressReg << "]\n";
                return;
            }
            const auto slot = slot_of.find(value);
            if (slot == slot_of.end()) {
                out << "  ld r" << kAddressReg << ", #0\n";
                out << "  fld f" << fregister << ", [r" << kAddressReg << "]\n";
                return;
            }
            out << "  mov r" << kAddressReg << ", r13\n";
            out << "  sub r" << kAddressReg << ", #" << (4u + 4u * slot->second) << "\n";
            out << "  fld f" << fregister << ", [r" << kAddressReg << "]\n";
        };        const auto store_result = [&](const ir::Instruction* instruction, unsigned reg) {
            if (instruction->type != nullptr && instruction->type->kind != ir::TypeKind::Void) {
                const auto slot = slot_of.find(instruction);
                if (slot != slot_of.end()) {
                    frame_store("st", slot->second, "r" + std::to_string(reg));
                }
            }
        };

        for (const auto& block : function->blocks) {
            out << ".Lbb" << block_index[block.get()] << ":\n";
            for (const auto& instruction : block->instructions) {
                const auto operand = [&](std::size_t index, unsigned reg) {
                    materialize(index < instruction->operands.size()
                                    ? instruction->operands[index]
                                    : nullptr,
                                reg);
                };
                switch (instruction->opcode) {
                    case ir::Opcode::Alloca:
                        break;
                    case ir::Opcode::Trunc:
                    case ir::Opcode::ZExt:
                    case ir::Opcode::SExt:
                    case ir::Opcode::BitCast:
                    case ir::Opcode::PtrToInt:
                    case ir::Opcode::IntToPtr: {
                        if (!instruction->operands.empty()) {
                            operand(0, kValueReg);
                            store_result(instruction.get(), kValueReg);
                        }
                        break;
                    }
                    case ir::Opcode::GetElementPtr: {
                        if (instruction->operands.empty()) {
                            error = "kr232 backend: getelementptr without a base";
                            return std::nullopt;
                        }
                        const auto* base_type = instruction->operands[0]->type != nullptr
                                                    ? instruction->operands[0]->type->element
                                                    : nullptr;
                        const std::uint32_t element = type_bytes(base_type);
                        const auto* base_instruction =
                            dynamic_cast<const ir::Instruction*>(instruction->operands[0]);
                        if (base_instruction != nullptr &&
                            base_instruction->opcode == ir::Opcode::Alloca) {
                            const auto slot = slot_of.find(base_instruction);
                            out << "  mov r" << kValueReg << ", r13\n";
                            out << "  sub r" << kValueReg << ", #" << (4u + 4u * slot->second)
                                << "\n";
                        } else {
                            operand(0, kValueReg);
                        }
                        // A record member index selects a field: its byte offset is
                        // the sum of the preceding fields' sizes, not the index
                        // times the whole record.
                        if (base_type != nullptr && base_type->kind == ir::TypeKind::Struct &&
                            instruction->operands.size() == 3) {
                            const auto* member =
                                dynamic_cast<const ir::ConstantInt*>(instruction->operands[2]);
                            if (member == nullptr) {
                                error = "kr232 backend: a variable member index is not implemented";
                                return std::nullopt;
                            }
                            const std::uint32_t offset_bytes =
                                field_offset(base_type, static_cast<std::size_t>(member->value));
                            if (offset_bytes != 0) {
                                out << "  add r" << kValueReg << ", #" << offset_bytes << "\n";
                            }
                            store_result(instruction.get(), kValueReg);
                            break;
                        }
                        for (std::size_t i = 1; i < instruction->operands.size(); ++i) {
                            const auto* index =
                                dynamic_cast<const ir::ConstantInt*>(instruction->operands[i]);
                            if (index != nullptr) {
                                const std::int64_t constant =
                                    index->value * static_cast<std::int64_t>(element);
                                if (constant != 0) {
                                    out << "  add r" << kValueReg << ", #" << constant << "\n";
                                }
                                continue;
                            }
                            operand(i, kSecondReg);
                            if (element == 2) {
                                out << "  shl r" << kSecondReg << ", #1\n";
                            } else if (element == 4) {
                                out << "  shl r" << kSecondReg << ", #2\n";
                            } else if (element == 8) {
                                out << "  shl r" << kSecondReg << ", #3\n";
                            } else if (element != 1) {
                                error = "kr232 backend: an element size that is not a power of two";
                                return std::nullopt;
                            }
                            out << "  add r" << kValueReg << ", r" << kSecondReg << "\n";
                        }
                        store_result(instruction.get(), kValueReg);
                        break;
                    }
                    case ir::Opcode::Load: {
                        if (instruction->operands.empty()) {
                            error = "kr232 backend: load without an address";
                            return std::nullopt;
                        }
                        // A floating-point value is two words: frame traffic uses
                        // `fld`/`fst` through a computed address, because the FP
                        // forms take a base register rather than a frame offset.
                        if (instruction->type != nullptr &&
                            instruction->type->kind == ir::TypeKind::Float) {
                            materialize(instruction->operands[0], kAddressReg);
                            out << "  fld f0, [r8]\n";
                            const auto slot = slot_of.find(instruction.get());
                            if (slot != slot_of.end()) {
                                out << "  mov r8, r13\n";
                                out << "  sub r8, #" << (4u + 4u * slot->second) << "\n";
                                out << "  fst f0, [r8]\n";
                            }
                            break;
                        }
                        // A record value is its address: loading one names where it
                        // lives rather than copying its bytes.
                        if (instruction->type != nullptr &&
                            (instruction->type->kind == ir::TypeKind::Struct ||
                             instruction->type->kind == ir::TypeKind::Union ||
                             instruction->type->kind == ir::TypeKind::Array)) {
                            materialize(instruction->operands[0], kValueReg);
                            store_result(instruction.get(), kValueReg);
                            break;
                        }
                        const auto bytes = type_bytes(instruction->type);
                        const bool is_signed =
                            instruction->type != nullptr && instruction->type->is_signed;
                        const std::string address = address_of(instruction->operands[0]);
                        if (bytes == 1) {
                            out << (is_signed ? "  ldbs r" : "  ldbu r") << kValueReg << ", "
                                << address << "\n";
                        } else if (bytes == 2) {
                            out << (is_signed ? "  ldhs r" : "  ldhu r") << kValueReg << ", "
                                << address << "\n";
                        } else {
                            out << "  ld r" << kValueReg << ", " << address << "\n";
                        }
                        store_result(instruction.get(), kValueReg);
                        break;
                    }
                    case ir::Opcode::Store: {
                        if (instruction->operands.size() < 2) {
                            error = "kr232 backend: store without an address";
                            return std::nullopt;
                        }
                        // A floating-point store moves the value through f0 and
                        // writes eight bytes at the computed address.
                        if (instruction->operands[0]->type != nullptr &&
                            instruction->operands[0]->type->kind == ir::TypeKind::Float) {
                            materialize_float(instruction->operands[0], 0);
                            materialize(instruction->operands[1], kAddressReg);
                            out << "  fst f0, [r8]\n";
                            break;
                        }
                        // A record store copies its bytes: the value is an address
                        // and so is the destination.
                        if (instruction->operands[0]->type != nullptr &&
                            (instruction->operands[0]->type->kind == ir::TypeKind::Struct ||
                             instruction->operands[0]->type->kind == ir::TypeKind::Union ||
                             instruction->operands[0]->type->kind == ir::TypeKind::Array)) {
                            const std::uint32_t bytes =
                                type_bytes(instruction->operands[0]->type);
                            materialize(instruction->operands[1], kSecondReg);  // destination
                            materialize(instruction->operands[0], kValueReg);   // source
                            for (std::uint32_t i = 0; i < bytes; ++i) {
                                out << "  ldbu r" << kAddressReg << ", [r" << kValueReg << "+" << i
                                    << "]\n";
                                out << "  stb [r" << kSecondReg << "+" << i << "], r"
                                    << kAddressReg << "\n";
                            }
                            break;
                        }
                        const auto bytes = type_bytes(instruction->operands[0]->type);
                        // The value first, then the address into R8: a global's
                        // address is an `ld`, and loading it first would overwrite
                        // the value.
                        materialize(instruction->operands[0], kValueReg);
                        const std::string address = address_of(instruction->operands[1]);
                        if (bytes == 1) {
                            out << "  stb " << address << ", r" << kValueReg << "\n";
                        } else if (bytes == 2) {
                            out << "  sth " << address << ", r" << kValueReg << "\n";
                        } else {
                            out << "  st " << address << ", r" << kValueReg << "\n";
                        }
                        break;
                    }
                    case ir::Opcode::FAdd:
                    case ir::Opcode::FSub:
                    case ir::Opcode::FMul:
                    case ir::Opcode::FDiv: {
                        if (instruction->operands.size() < 2) {
                            error = "kr232 backend: a floating-point operation needs two operands";
                            return std::nullopt;
                        }
                        const char* op_name = "fadd";
                        switch (instruction->opcode) {
                            case ir::Opcode::FAdd: op_name = "fadd"; break;
                            case ir::Opcode::FSub: op_name = "fsub"; break;
                            case ir::Opcode::FMul: op_name = "fmul"; break;
                            default: op_name = "fdiv"; break;
                        }
                        // Floating-point values live in frame slots like everything
                        // else: two words each, moved through f6 and f7.
                        materialize_float(instruction->operands[0], 0);
                        materialize_float(instruction->operands[1], 1);
                        out << "  " << op_name << " f0, f1\n";
                        // The FP store names its register first: `fst fD, [addr]`.
                        {
                            const std::uint32_t offset = 4u + 4u * slot_of.at(instruction.get());
                            if (offset > 128u) {
                                out << "  mov r8, r13\n";
                                out << "  sub r8, #" << offset << "\n";
                                out << "  fst f0, [r8]\n";
                            } else {
                                out << "  fst f0, [r13-" << offset << "]\n";
                            }
                        }
                        break;
                    }
                    case ir::Opcode::FPToSI:
                    case ir::Opcode::FPToUI: {
                        if (instruction->operands.empty()) {
                            error = "kr232 backend: an FP conversion needs an operand";
                            return std::nullopt;
                        }
                        materialize_float(instruction->operands[0], 0);
                        out << "  " << (instruction->opcode == ir::Opcode::FPToSI ? "f2i" : "f2u")
                            << " r6, f0\n";
                        store_result(instruction.get(), kValueReg);
                        break;
                    }
                    case ir::Opcode::SIToFP:
                    case ir::Opcode::UIToFP: {
                        if (instruction->operands.empty()) {
                            error = "kr232 backend: an FP conversion needs an operand";
                            return std::nullopt;
                        }
                        materialize(instruction->operands[0], kValueReg);
                        out << "  " << (instruction->opcode == ir::Opcode::SIToFP ? "i2f" : "u2f")
                            << " f0, r6\n";
                        // The FP store forms take a base register, not a frame offset.
                        out << "  mov r8, r13\n";
                        out << "  sub r8, #" << (4u + 4u * slot_of.at(instruction.get()))
                            << "\n";
                        out << "  fst f0, [r8]\n";
                        break;
                    }
                    case ir::Opcode::Add:
                    case ir::Opcode::Sub:
                    case ir::Opcode::Mul:
                    case ir::Opcode::And:
                    case ir::Opcode::Or:
                    case ir::Opcode::Xor:
                    case ir::Opcode::Shl:
                    case ir::Opcode::LShr:
                    case ir::Opcode::AShr:
                    case ir::Opcode::UDiv:
                    case ir::Opcode::SDiv: {
                        if (instruction->operands.size() < 2) {
                            error = "kr232 backend: binary operation without two operands";
                            return std::nullopt;
                        }
                        operand(0, kValueReg);
                        operand(1, kSecondReg);
                        const char* op_name = "add";
                        switch (instruction->opcode) {
                            case ir::Opcode::Add: op_name = "add"; break;
                            case ir::Opcode::Sub: op_name = "sub"; break;
                            case ir::Opcode::Mul: op_name = "mul"; break;
                            case ir::Opcode::And: op_name = "and"; break;
                            case ir::Opcode::Or: op_name = "or"; break;
                            case ir::Opcode::Xor: op_name = "xor"; break;
                            case ir::Opcode::Shl: op_name = "shl"; break;
                            case ir::Opcode::LShr: op_name = "shr"; break;
                            case ir::Opcode::AShr: op_name = "sar"; break;
                            case ir::Opcode::UDiv: op_name = "div"; break;
                            default: op_name = "divs"; break;
                        }
                        out << "  " << op_name << " r" << kValueReg << ", r" << kSecondReg << "\n";
                        store_result(instruction.get(), kValueReg);
                        break;
                    }
                    case ir::Opcode::URem:
                    case ir::Opcode::SRem:
                        error = "kr232 backend: remainder is not implemented";
                        return std::nullopt;
                    case ir::Opcode::ICmp: {
                        if (instruction->operands.size() < 2) {
                            error = "kr232 backend: comparison without two operands";
                            return std::nullopt;
                        }
                        operand(0, kValueReg);
                        operand(1, kSecondReg);
                        const std::string tail = ".Lcmp" + std::to_string(next_compare++);
                        const char* branch = "beq";
                        switch (instruction->icmp_predicate) {
                            case ir::ICmpPredicate::Eq: branch = "beq"; break;
                            case ir::ICmpPredicate::Ne: branch = "bne"; break;
                            case ir::ICmpPredicate::Ugt: branch = "bhi"; break;
                            case ir::ICmpPredicate::Uge: branch = "bcs"; break;
                            case ir::ICmpPredicate::Ult: branch = "bcc"; break;
                            case ir::ICmpPredicate::Ule: branch = "bls"; break;
                            case ir::ICmpPredicate::Sgt: branch = "bgt"; break;
                            case ir::ICmpPredicate::Sge: branch = "bge"; break;
                            case ir::ICmpPredicate::Slt: branch = "blt"; break;
                            case ir::ICmpPredicate::Sle: branch = "ble"; break;
                        }
                        out << "  cmp r" << kValueReg << ", r" << kSecondReg << "\n";
                        out << "  ld r" << kValueReg << ", #0\n";
                        out << "  " << branch << " " << tail << "\n";
                        out << "  bra " << tail << "end\n";
                        out << tail << ":\n";
                        out << "  ld r" << kValueReg << ", #1\n";
                        out << tail << "end:\n";
                        store_result(instruction.get(), kValueReg);
                        break;
                    }
                    case ir::Opcode::Br: {
                        if (instruction->successors.empty()) {
                            error = "kr232 backend: branch without a target";
                            return std::nullopt;
                        }
                        out << "  bra .Lbb" << block_index[instruction->successors[0]] << "\n";
                        break;
                    }
                    case ir::Opcode::CondBr: {
                        if (instruction->operands.empty() || instruction->successors.size() < 2) {
                            error = "kr232 backend: conditional branch without a condition";
                            return std::nullopt;
                        }
                        operand(0, kValueReg);
                        out << "  ld r" << kSecondReg << ", #0\n";
                        out << "  cmp r" << kValueReg << ", r" << kSecondReg << "\n";
                        out << "  bne .Lbb" << block_index[instruction->successors[0]] << "\n";
                        out << "  bra .Lbb" << block_index[instruction->successors[1]] << "\n";
                        break;
                    }
                    case ir::Opcode::Call: {
                        if (instruction->operands.empty()) {
                            error = "kr232 backend: call without a callee";
                            return std::nullopt;
                        }
                        const auto* callee =
                            dynamic_cast<const ir::Function*>(instruction->operands[0]);
                        if (callee == nullptr) {
                            // An indirect call: a virtual dispatch reaches a function
                            // pointer loaded from the vtable. The ISA has no call
                            // through a register, so the return address is pushed and
                            // the callee is reached with `jmp rD` -- `ret` pops what
                            // was pushed, exactly as `call` would have left it.
                            if (instruction->operands.size() > 5) {
                                error =
                                    "kr232 backend: more than four arguments needs the stack form";
                                return std::nullopt;
                            }
                            for (std::size_t i = 1; i < instruction->operands.size(); ++i) {
                                materialize(instruction->operands[i],
                                            2u + static_cast<unsigned>(i - 1));
                            }
                            const std::string back = ".Lcall" + std::to_string(next_indirect++);
                            materialize(instruction->operands[0], kAddressReg);
                            out << "  ld r" << kSecondReg << ", #" << back << "\n";
                            out << "  push r" << kSecondReg << "\n";
                            out << "  jmp r" << kAddressReg << "\n";
                            out << back << ":\n";
                            if (instruction->type != nullptr &&
                                instruction->type->kind != ir::TypeKind::Void) {
                                out << "  mov r" << kValueReg << ", r2\n";
                                store_result(instruction.get(), kValueReg);
                            }
                            break;
                        }
                        if (instruction->operands.size() > 5) {
                            error = "kr232 backend: more than four arguments needs the stack form";
                            return std::nullopt;
                        }
                        for (std::size_t i = 1; i < instruction->operands.size(); ++i) {
                            materialize(instruction->operands[i],
                                        2u + static_cast<unsigned>(i - 1));
                        }
                        // A record return needs a destination the callee copies
                        // into; the caller names it in R6.
                        const auto destination = aggregate_destination.find(instruction.get());
                        if (destination != aggregate_destination.end()) {
                            out << "  mov r6, r13\n";
                            out << "  sub r6, #" << (4u + 4u * destination->second) << "\n";
                        }
                        out << "  call " << callee->name << "\n";
                        if (instruction->type != nullptr &&
                            instruction->type->kind == ir::TypeKind::Float) {
                            // A floating-point result comes back in F0.
                            out << "  mov r8, r13\n";
                            out << "  sub r8, #" << (4u + 4u * slot_of.at(instruction.get()))
                                << "\n";
                            out << "  fst f0, [r8]\n";
                        } else if (instruction->type != nullptr &&
                                   instruction->type->kind != ir::TypeKind::Void) {
                            out << "  mov r" << kValueReg << ", r2\n";
                            store_result(instruction.get(), kValueReg);
                        }
                        break;
                    }
                    case ir::Opcode::Ret: {
                        if (instruction->operands.empty()) {
                            out << "  ld r2, #0\n";
                        } else if (function->type != nullptr &&
                                   function->type->return_type != nullptr &&
                                   function->type->return_type->kind == ir::TypeKind::Float) {
                            // A floating-point result is returned in F0.
                            materialize_float(instruction->operands[0], 0);
                        } else if (returns_record) {
                            // Copy the record out to the destination the caller named
                            // in R6, and hand that pointer back in R2.
                            const std::uint32_t bytes =
                                type_bytes(function->type->return_type);
                            materialize(instruction->operands[0], kValueReg);
                            frame_load("ld", return_pointer_slot,
                                       "r" + std::to_string(kSecondReg));
                            for (std::uint32_t i = 0; i < bytes; ++i) {
                                out << "  ldbu r" << kAddressReg << ", [r" << kValueReg << "+" << i
                                    << "]\n";
                                out << "  stb [r" << kSecondReg << "+" << i << "], r"
                                    << kAddressReg << "\n";
                            }
                            out << "  mov r2, r" << kSecondReg << "\n";
                        } else {
                            materialize(instruction->operands[0], 2);
                        }
                        out << "  leave #0\n";
                        out << "  ret\n";
                        break;
                    }
                    default:
                        error = "kr232 backend: opcode " +
                                std::to_string(static_cast<int>(instruction->opcode)) +
                                " is not implemented yet";
                        return std::nullopt;
                }
            }
        }
    }
    return out.str();
}

}  // namespace krc::backend
