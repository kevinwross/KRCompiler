#include "kr232_backend.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace krc::backend {
namespace {

// The ABI's frame is a multiple of the 4-byte stack alignment.
std::uint32_t align_frame(std::uint32_t bytes) { return (bytes + 3u) & ~3u; }

// kr232 sizes: a pointer is 32 bits, an integer or float is `bits` wide, an array
// is its element times its count, a record is its fields laid out at 4-byte
// alignment. `type_size` is private to each backend, so this backend owns its own.
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
            std::uint32_t bytes = 0;
            for (const ir::Type* field : type->fields) {
                bytes += align_frame(type_bytes(field));
            }
            return bytes;
        }
        case ir::TypeKind::Function:
            return 0;
    }
    return 0;
}

// The registers this first pass uses: R6 and R7 are caller-saved scratch, and R2
// is the return value.
constexpr unsigned kScratch[] = {6, 7};

}  // namespace

Kr232Backend::Kr232Backend(const ir::Module& module, const target::TargetProfile& profile,
                           OptimizationLevel level)
    : module_(module), profile_(profile), level_(level) {}

std::uint32_t Kr232Backend::frame_bytes(const ir::Function& function) const {
    std::uint32_t bytes = 0;
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block->instructions) {
            if (instruction->opcode == ir::Opcode::Alloca) {
                bytes += align_frame(type_bytes(instruction->allocated_type));
            }
        }
    }
    return align_frame(bytes);
}

std::optional<std::string> Kr232Backend::emit(std::string& error) const {
    debug_info_.clear();
    std::ostringstream out;
    out << "; kr232 backend, optimization level " << static_cast<int>(level_) << "\n";
    for (const auto& function : module_.functions()) {
        const std::uint32_t frame = align_frame(frame_bytes(*function));
        const std::string name = function->name;
        out << ".globl " << name << "\n";
        out << ".text\n";
        out << name << ":\n";
        // ENTER saves FP, pushes the mask and reserves the frame; reset SP is
        // 0x08000000, so nothing sets the stack up.
        out << "  enter #" << frame << ", #0\n";

        std::unordered_map<const ir::Value*, std::uint32_t> slot_of;
        std::unordered_map<const ir::Value*, unsigned> reg_of;
        std::uint32_t next_slot = 0;
        unsigned next_scratch = 0;
        bool returned = false;

        // A value's home register. Constants get one too: materialising them is
        // the caller's job, and a constant that is never materialised is how a
        // `mov r6, r6` appears in the output.
        const auto register_for = [&](const ir::Value* value) -> unsigned {
            const auto found = reg_of.find(value);
            if (found != reg_of.end()) {
                return found->second;
            }
            const unsigned reg = kScratch[next_scratch % 2];
            ++next_scratch;
            reg_of.emplace(value, reg);
            return reg;
        };
        const auto materialize = [&](const ir::Value* value, unsigned reg) {
            if (const auto* constant = dynamic_cast<const ir::ConstantInt*>(value)) {
                out << "  ld r" << reg << ", #" << constant->value << "\n";
                return;
            }
            const unsigned source = register_for(value);
            if (source != reg) {
                out << "  mov r" << reg << ", r" << source << "\n";
            }
        };
        // A frame slot's address, as an assembler operand.
        const auto slot_address = [&](std::uint32_t slot) {
            return "[r13-" + std::to_string(4 + 4 * slot) + "]";
        };

        for (const auto& block : function->blocks) {
            for (const auto& instruction : block->instructions) {
                switch (instruction->opcode) {
                    case ir::Opcode::Alloca: {
                        slot_of.emplace(instruction.get(), next_slot++);
                        break;
                    }
                    case ir::Opcode::Trunc:
                    case ir::Opcode::ZExt:
                    case ir::Opcode::SExt: {
                        // A cast changes nothing in a register: the store writes the
                        // low bytes and the load sign- or zero-extends.
                        if (instruction->operands.empty()) {
                            error = "kr232 backend: cast without an operand";
                            return std::nullopt;
                        }
                        const unsigned reg = register_for(instruction->operands[0]);
                        materialize(instruction->operands[0], reg);
                        reg_of[instruction.get()] = reg;
                        break;
                    }
                    case ir::Opcode::Store: {
                        if (instruction->operands.size() < 2) {
                            error = "kr232 backend: store without an address";
                            return std::nullopt;
                        }
                        const auto slot = slot_of.find(instruction->operands[1]);
                        if (slot == slot_of.end()) {
                            error = "kr232 backend: only frame slots can be stored to yet";
                            return std::nullopt;
                        }
                        const unsigned reg = register_for(instruction->operands[0]);
                        materialize(instruction->operands[0], reg);
                        const auto bytes = type_bytes(instruction->operands[0]->type);
                        const std::string address = slot_address(slot->second);
                        if (bytes == 1) {
                            out << "  stb " << address << ", r" << reg << "\n";
                        } else if (bytes == 2) {
                            out << "  sth " << address << ", r" << reg << "\n";
                        } else {
                            out << "  st " << address << ", r" << reg << "\n";
                        }
                        break;
                    }
                    case ir::Opcode::Load: {
                        if (instruction->operands.empty()) {
                            error = "kr232 backend: load without an address";
                            return std::nullopt;
                        }
                        const auto slot = slot_of.find(instruction->operands[0]);
                        if (slot == slot_of.end()) {
                            error = "kr232 backend: only frame slots can be loaded yet";
                            return std::nullopt;
                        }
                        const unsigned reg = register_for(instruction.get());
                        const auto bytes = type_bytes(instruction->type);
                        const bool is_signed =
                            instruction->type != nullptr && instruction->type->is_signed;
                        const std::string address = slot_address(slot->second);
                        if (bytes == 1) {
                            out << (is_signed ? "  ldbs r" : "  ldbu r") << reg << ", " << address
                                << "\n";
                        } else if (bytes == 2) {
                            out << (is_signed ? "  ldhs r" : "  ldhu r") << reg << ", " << address
                                << "\n";
                        } else {
                            out << "  ld r" << reg << ", " << address << "\n";
                        }
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
                    case ir::Opcode::AShr: {
                        if (instruction->operands.size() < 2) {
                            error = "kr232 backend: binary operation without two operands";
                            return std::nullopt;
                        }
                        const ir::Value* left = instruction->operands[0];
                        const ir::Value* right = instruction->operands[1];
                        const unsigned reg = register_for(instruction.get());
                        materialize(left, reg);
                        const auto* constant = dynamic_cast<const ir::ConstantInt*>(right);
                        const char* op_name = nullptr;
                        switch (instruction->opcode) {
                            case ir::Opcode::Add: op_name = "add"; break;
                            case ir::Opcode::Sub: op_name = "sub"; break;
                            case ir::Opcode::Mul: op_name = "mul"; break;
                            case ir::Opcode::And: op_name = "and"; break;
                            case ir::Opcode::Or: op_name = "or"; break;
                            case ir::Opcode::Xor: op_name = "xor"; break;
                            case ir::Opcode::Shl: op_name = "shl"; break;
                            case ir::Opcode::LShr: op_name = "shr"; break;
                            default: op_name = "sar"; break;
                        }
                        if (constant != nullptr) {
                            out << "  " << op_name << " r" << reg << ", #" << constant->value << "\n";
                        } else {
                            out << "  " << op_name << " r" << reg << ", r" << register_for(right)
                                << "\n";
                        }
                        break;
                    }
                    case ir::Opcode::Ret: {
                        const ir::Value* value =
                            instruction->operands.empty() ? nullptr : instruction->operands[0];
                        if (value == nullptr) {
                            out << "  ld r2, #0\n";
                        } else {
                            materialize(value, 2);
                        }
                        out << "  leave #0\n";
                        out << "  ret\n";
                        returned = true;
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
        if (!returned) {
            out << "  ld r2, #0\n";
            out << "  leave #0\n";
            out << "  ret\n";
        }
    }
    return out.str();
}

}  // namespace krc::backend
