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
    // Globals live in `.data`; each is one word unless it holds an array of bytes.
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
        // A declaration names a function defined elsewhere; emitting a body for
        // it would define a duplicate symbol.
        if (function->declaration) {
            continue;
        }
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
        std::uint32_t next_compare = 0;
        unsigned next_scratch = 0;
        bool returned = false;
        // Branch targets are block labels, numbered in function order.
        std::unordered_map<const ir::BasicBlock*, std::size_t> block_index;
        for (std::size_t i = 0; i < function->blocks.size(); ++i) {
            block_index.emplace(function->blocks[i].get(), i);
        }
        // Arguments arrive in R2-R5 (kr232 ABI), so a parameter's home register is
        // the one the caller put it in. More than four needs the stack form the ABI
        // also defines, which this pass does not emit yet.
        {
            if (function->parameters.size() > 4) {
                error = "kr232 backend: more than four arguments needs the stack form";
                return std::nullopt;
            }
            for (std::size_t i = 0; i < function->parameters.size(); ++i) {
                reg_of.emplace(function->parameters[i].get(), 2u + static_cast<unsigned>(i));
            }
        }

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
            // A global is addressed by its symbol: the store or load that follows
            // uses the register the linker filled with the symbol's address.
            if (const auto* global = dynamic_cast<const ir::GlobalVariable*>(value)) {
                out << "  ld r" << reg << ", #" << global->name << "\n";
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
            out << ".Lbb" << block_index[block.get()] << ":\n";
            for (const auto& instruction : block->instructions) {
                switch (instruction->opcode) {
                    case ir::Opcode::Alloca: {
                        slot_of.emplace(instruction.get(), next_slot++);
                        break;
                    }
                    case ir::Opcode::Trunc:
                    case ir::Opcode::ZExt:
                    case ir::Opcode::SExt:
                    case ir::Opcode::BitCast:
                    case ir::Opcode::PtrToInt:
                    case ir::Opcode::IntToPtr: {
                        // A cast changes nothing in a register: the store writes the
                        // low bytes and the load sign- or zero-extends. The pointer
                        // casts are the same: an address is a 32-bit value.
                        if (instruction->operands.empty()) {
                            error = "kr232 backend: cast without an operand";
                            return std::nullopt;
                        }
                        const unsigned reg = register_for(instruction->operands[0]);
                        materialize(instruction->operands[0], reg);
                        reg_of[instruction.get()] = reg;
                        break;
                    }
                    case ir::Opcode::GetElementPtr: {
                        if (instruction->operands.empty()) {
                            error = "kr232 backend: getelementptr without a base";
                            return std::nullopt;
                        }
                        // A constant index folds to one displacement: `&p[i]` where
                        // i is known is the base plus i times the element size. A
                        // variable index is scaled and added at run time.
                        std::int64_t offset = 0;
                        const auto* base_type =
                            instruction->operands[0]->type != nullptr
                                ? instruction->operands[0]->type->element
                                : nullptr;
                        const std::uint32_t element = type_bytes(base_type);
                        const unsigned reg = register_for(instruction.get());
                        materialize(instruction->operands[0], reg);
                        for (std::size_t i = 1; i < instruction->operands.size(); ++i) {
                            const auto* index =
                                dynamic_cast<const ir::ConstantInt*>(instruction->operands[i]);
                            if (index != nullptr) {
                                offset += index->value * static_cast<std::int64_t>(element);
                                continue;
                            }
                            const unsigned other = reg == 6 ? 7 : 6;
                            materialize(instruction->operands[i], other);
                            if (element == 2) {
                                out << "  shl r" << other << ", #1\n";
                            } else if (element == 4) {
                                out << "  shl r" << other << ", #2\n";
                            } else if (element == 8) {
                                out << "  shl r" << other << ", #3\n";
                            } else if (element != 1) {
                                error = "kr232 backend: an element size that is not a power of two";
                                return std::nullopt;
                            }
                            out << "  add r" << reg << ", r" << other << "\n";
                        }
                        if (offset != 0) {
                            out << "  add r" << reg << ", #" << offset << "\n";
                        }
                        break;
                    }
                    case ir::Opcode::Store: {
                        if (instruction->operands.size() < 2) {
                            error = "kr232 backend: store without an address";
                            return std::nullopt;
                        }
                        const auto slot = slot_of.find(instruction->operands[1]);
                        const unsigned reg = register_for(instruction->operands[0]);
                        materialize(instruction->operands[0], reg);
                        const auto bytes = type_bytes(instruction->operands[0]->type);
                        const bool in_frame = slot != slot_of.end();
                        const unsigned addr_reg = in_frame ? 13u : (reg == 7 ? 6 : 7);
                        if (!in_frame) {
                            materialize(instruction->operands[1], addr_reg);
                        }
                        const std::string address = in_frame
                                                        ? slot_address(slot->second)
                                                        : "[r" + std::to_string(addr_reg) + "]";
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
                        const unsigned reg = register_for(instruction.get());
                        const auto bytes = type_bytes(instruction->type);
                        const bool in_frame = slot != slot_of.end();
                        const unsigned load_addr = reg == 7 ? 6 : 7;
                        if (!in_frame) {
                            materialize(instruction->operands[0], load_addr);
                        }
                        const bool is_signed =
                            instruction->type != nullptr && instruction->type->is_signed;
                        const std::string address = in_frame ? slot_address(slot->second)
                                                              : "[r" + std::to_string(load_addr) + "]";
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
                    case ir::Opcode::AShr:
                    case ir::Opcode::UDiv:
                    case ir::Opcode::SDiv:
                    case ir::Opcode::URem:
                    case ir::Opcode::SRem: {
                        if (instruction->operands.size() < 2) {
                            error = "kr232 backend: binary operation without two operands";
                            return std::nullopt;
                        }
                        const ir::Value* left = instruction->operands[0];
                        const ir::Value* right = instruction->operands[1];
                        // Neither division has a remainder form and the two-register
                        // model has no third scratch for quotient, product and
                        // result; a remainder is refused by name rather than emitted
                        // as a division.
                        if (instruction->opcode == ir::Opcode::URem ||
                            instruction->opcode == ir::Opcode::SRem) {
                            error = "kr232 backend: remainder is not implemented";
                            return std::nullopt;
                        }
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
                            case ir::Opcode::UDiv: op_name = "div"; break;
                            case ir::Opcode::SDiv: op_name = "divs"; break;
                            case ir::Opcode::URem: op_name = "srem"; break;
                            case ir::Opcode::SRem: op_name = "srem"; break;
                            default: op_name = "sar"; break;
                        }
                        const bool register_form = constant == nullptr ||
                                                   instruction->opcode == ir::Opcode::UDiv ||
                                                   instruction->opcode == ir::Opcode::SDiv ||
                                                   instruction->opcode == ir::Opcode::URem ||
                                                   instruction->opcode == ir::Opcode::SRem;
                        if (!register_form) {
                            out << "  " << op_name << " r" << reg << ", #" << constant->value << "\n";
                        } else {
                            const unsigned divisor = reg == 6 ? 7 : 6;
                            materialize(right, divisor);
                            out << "  " << op_name << " r" << reg << ", r" << divisor << "\n";
                        }
                        break;
                    }
                    case ir::Opcode::ICmp: {
                        if (instruction->operands.size() < 2) {
                            error = "kr232 backend: comparison without two operands";
                            return std::nullopt;
                        }
                        const unsigned reg = register_for(instruction.get());
                        const unsigned other = reg == 6 ? 7 : 6;
                        materialize(instruction->operands[0], reg);
                        materialize(instruction->operands[1], other);
                        // A nested SETcc has no one-operand form in the assembler
                        // yet, so the boolean is materialised through a branch: the
                        // value starts at zero and the taken path loads one.
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
                        out << "  cmp r" << reg << ", r" << other << "\n";
                        out << "  ld r" << reg << ", #0\n";
                        out << "  " << branch << " " << tail << "\n";
                        out << "  bra " << tail << "end\n";
                        out << tail << ":\n";
                        out << "  ld r" << reg << ", #1\n";
                        out << tail << "end:\n";
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
                        const unsigned reg = register_for(instruction->operands[0]);
                        materialize(instruction->operands[0], reg);
                        const unsigned zero = reg == 6 ? 7 : 6;
                        out << "  ld r" << zero << ", #0\n";
                        out << "  cmp r" << reg << ", r" << zero << "\n";
                        out << "  bne .Lbb" << block_index[instruction->successors[0]] << "\n";
                        out << "  bra .Lbb" << block_index[instruction->successors[1]] << "\n";
                        break;
                    }
                    case ir::Opcode::Call: {
                        if (instruction->operands.empty()) {
                            error = "kr232 backend: call without a callee";
                            return std::nullopt;
                        }
                        const auto* callee = dynamic_cast<const ir::Function*>(instruction->operands[0]);
                        if (callee == nullptr) {
                            error = "kr232 backend: only a direct call to a function is implemented";
                            return std::nullopt;
                        }
                        // Arguments go in R2-R5 before the call; the result comes
                        // back in R2, so it moves into this value's register.
                        for (std::size_t i = 1; i < instruction->operands.size(); ++i) {
                            materialize(instruction->operands[i], 2u + static_cast<unsigned>(i - 1));
                        }
                        out << "  call " << callee->name << "\n";
                        const bool returns_value =
                            instruction->type != nullptr && instruction->type->kind != ir::TypeKind::Void;
                        if (returns_value) {
                            const unsigned reg = register_for(instruction.get());
                            if (reg != 2) {
                                out << "  mov r" << reg << ", r2\n";
                            }
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
