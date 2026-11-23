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
    if (!module_.globals().empty()) {
        out << ".data\n";
        for (const auto& global : module_.globals()) {
            if (global->external) {
                continue;
            }
            out << global->name << ":\n";
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
        // value, then one per parameter.
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
                    slot_of.emplace(instruction.get(), next_slot++);
                }
            }
        }
        for (const auto& parameter : function->parameters) {
            slot_of.emplace(parameter.get(), next_slot++);
        }
        const std::uint32_t frame = align_frame(next_slot * 4u);

        out << "  enter #" << frame << ", #0\n";
        for (std::size_t i = 0; i < function->parameters.size(); ++i) {
            const auto slot = slot_of.at(function->parameters[i].get());
            out << "  st [r13-" << (4u + 4u * slot) << "], r"
                << (2u + static_cast<unsigned>(i)) << "\n";
        }

        std::uint32_t next_compare = 0;
        std::unordered_map<const ir::BasicBlock*, std::size_t> block_index;
        for (std::size_t i = 0; i < function->blocks.size(); ++i) {
            block_index.emplace(function->blocks[i].get(), i);
        }
        const auto slot_address = [](std::uint32_t slot) {
            return "[r13-" + std::to_string(4 + 4 * slot) + "]";
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
            const auto slot = slot_of.find(value);
            if (slot == slot_of.end()) {
                out << "  ld r" << reg << ", #0\n";
                return;
            }
            out << "  ld r" << reg << ", " << slot_address(slot->second) << "\n";
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
        const auto store_result = [&](const ir::Instruction* instruction, unsigned reg) {
            if (instruction->type != nullptr && instruction->type->kind != ir::TypeKind::Void) {
                const auto slot = slot_of.find(instruction);
                if (slot != slot_of.end()) {
                    out << "  st " << slot_address(slot->second) << ", r" << reg << "\n";
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
                            out << "  ld r" << kValueReg << ", r13\n";
                            out << "  sub r" << kValueReg << ", #" << (4u + 4u * slot->second)
                                << "\n";
                        } else {
                            operand(0, kValueReg);
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
                            error = "kr232 backend: only a direct call is implemented";
                            return std::nullopt;
                        }
                        if (instruction->operands.size() > 5) {
                            error = "kr232 backend: more than four arguments needs the stack form";
                            return std::nullopt;
                        }
                        for (std::size_t i = 1; i < instruction->operands.size(); ++i) {
                            materialize(instruction->operands[i],
                                        2u + static_cast<unsigned>(i - 1));
                        }
                        out << "  call " << callee->name << "\n";
                        if (instruction->type != nullptr &&
                            instruction->type->kind != ir::TypeKind::Void) {
                            out << "  mov r" << kValueReg << ", r2\n";
                            store_result(instruction.get(), kValueReg);
                        }
                        break;
                    }
                    case ir::Opcode::Ret: {
                        if (instruction->operands.empty()) {
                            out << "  ld r2, #0\n";
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
