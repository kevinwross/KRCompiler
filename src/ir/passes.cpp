#include "ir/passes.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace krc::ir {

namespace {

// The signed value of a constant operand, following a fold already recorded for
// it so a chain of folds resolves in one pass.
std::optional<std::int64_t> constant_operand(
    const Value* value, const std::map<const Value*, ConstantInt*>& folded) {
    if (!value) {
        return std::nullopt;
    }
    if (value->kind == ValueKind::ConstantInt) {
        return static_cast<const ConstantInt*>(value)->value;
    }
    const auto found = folded.find(value);
    if (found != folded.end()) {
        return found->second->value;
    }
    return std::nullopt;
}

// Sign-extends the low `bits` of `value`, which is the bit pattern a word-width
// operation leaves in a register.
std::int64_t sign_extend(std::uint64_t value, unsigned bits) {
    if (bits >= 64) {
        return static_cast<std::int64_t>(value);
    }
    const std::uint64_t sign_bit = std::uint64_t{1} << (bits - 1);
    const std::uint64_t mask = (std::uint64_t{1} << bits) - 1;
    const auto narrowed = value & mask;
    return (narrowed & sign_bit) != 0
               ? static_cast<std::int64_t>(narrowed | ~mask)
               : static_cast<std::int64_t>(narrowed);
}

// The constant an instruction folds to, or null when it must stay.
ConstantInt* fold_instruction(Module& module, const Instruction& instruction,
                              const std::map<const Value*, ConstantInt*>& folded) {
    if (!instruction.type || instruction.type->kind != TypeKind::Integer) {
        return nullptr;
    }
    const unsigned bits = static_cast<unsigned>(instruction.type->bits);
    if (bits != 32 && bits != 64) {
        return nullptr;
    }
    if (instruction.operands.size() != 2) {
        return nullptr;
    }
    const auto left = constant_operand(instruction.operands[0], folded);
    const auto right = constant_operand(instruction.operands[1], folded);
    if (!left || !right) {
        return nullptr;
    }

    const auto unsigned_left = static_cast<std::uint64_t>(*left);
    const auto unsigned_right = static_cast<std::uint64_t>(*right);
    std::uint64_t result = 0;
    switch (instruction.opcode) {
    case Opcode::Add:
        result = unsigned_left + unsigned_right;
        break;
    case Opcode::Sub:
        result = unsigned_left - unsigned_right;
        break;
    case Opcode::Mul:
        result = unsigned_left * unsigned_right;
        break;
    case Opcode::And:
        result = unsigned_left & unsigned_right;
        break;
    case Opcode::Or:
        result = unsigned_left | unsigned_right;
        break;
    case Opcode::Xor:
        result = unsigned_left ^ unsigned_right;
        break;
    case Opcode::Shl:
    case Opcode::LShr:
    case Opcode::AShr: {
        // The backend takes the count modulo the word size, so a count at or
        // beyond the width is not the same operation and is left alone.
        if (*right < 0 || static_cast<std::uint64_t>(*right) >= bits) {
            return nullptr;
        }
        const auto count = static_cast<unsigned>(*right);
        switch (instruction.opcode) {
        case Opcode::Shl:
            result = unsigned_left << count;
            break;
        case Opcode::LShr:
            result = (instruction.type->bits == 64
                          ? unsigned_left
                          : static_cast<std::uint64_t>(sign_extend(unsigned_left, 32))) >>
                     count;
            break;
        default:
            result = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(
                    instruction.type->bits == 64
                        ? static_cast<std::int64_t>(unsigned_left)
                        : sign_extend(unsigned_left, 32)) >>
                count);
            break;
        }
        break;
    }
    default:
        return nullptr;
    }
    return module.constant_int(instruction.type,
                               sign_extend(result, static_cast<unsigned>(bits)));
}

}  // namespace

PassChange constant_folding(Module& module) {
    PassChange change;
    for (const auto& function : module.functions()) {
        if (!function || function->declaration) {
            continue;
        }
        std::map<const Value*, ConstantInt*> folded;
        for (const auto& block : function->blocks) {
            if (!block) {
                continue;
            }
            for (const auto& instruction : block->instructions) {
                if (!instruction) {
                    continue;
                }
                if (auto* constant = fold_instruction(module, *instruction, folded)) {
                    folded[instruction.get()] = constant;
                }
            }
        }
        if (folded.empty()) {
            continue;
        }
        change.instructions_folded += folded.size();
        for (const auto& block : function->blocks) {
            if (!block) {
                continue;
            }
            auto& instructions = block->instructions;
            for (const auto& instruction : instructions) {
                if (!instruction) {
                    continue;
                }
                for (auto*& operand : instruction->operands) {
                    const auto found = folded.find(operand);
                    if (found != folded.end()) {
                        operand = found->second;
                    }
                }
            }
            const auto dead = std::remove_if(
                instructions.begin(), instructions.end(),
                [&](const std::unique_ptr<Instruction>& instruction) {
                    return instruction && folded.count(instruction.get()) != 0;
                });
            change.instructions_removed +=
                static_cast<std::size_t>(std::distance(dead, instructions.end()));
            instructions.erase(dead, instructions.end());
        }
    }
    return change;
}

namespace {

// A pure expression two instructions can share. The type, both predicates, the
// source type of a `getelementptr` and the operand sequence all take part: two
// GEPs over the same operands but different source types are different values,
// and a signed and an unsigned comparison of the same pair are different
// results.
struct ExpressionKey {
    Opcode opcode = Opcode::Unreachable;
    TypePtr type;
    TypePtr source_type;
    ICmpPredicate icmp_predicate = ICmpPredicate::Eq;
    FCmpPredicate fcmp_predicate = FCmpPredicate::False;
    std::vector<const Value*> operands;

    [[nodiscard]] bool operator<(const ExpressionKey& other) const {
        if (opcode != other.opcode) {
            return opcode < other.opcode;
        }
        if (type != other.type) {
            return type < other.type;
        }
        if (source_type != other.source_type) {
            return source_type < other.source_type;
        }
        if (icmp_predicate != other.icmp_predicate) {
            return icmp_predicate < other.icmp_predicate;
        }
        if (fcmp_predicate != other.fcmp_predicate) {
            return fcmp_predicate < other.fcmp_predicate;
        }
        return std::lexicographical_compare(
            operands.begin(), operands.end(), other.operands.begin(),
            other.operands.end());
    }
};

// The pure, non-trapping operations this pass will share. Everything with a
// memory effect, a trap, or an ordering is absent by construction, so the
// whitelist is the safety argument rather than a guard after the fact.
bool is_shareable(const Instruction& instruction) {
    if (instruction.is_volatile) {
        return false;
    }
    switch (instruction.opcode) {
    case Opcode::Add:
    case Opcode::Sub:
    case Opcode::Mul:
    case Opcode::And:
    case Opcode::Or:
    case Opcode::Xor:
    case Opcode::Shl:
    case Opcode::LShr:
    case Opcode::AShr:
    case Opcode::ICmp:
    case Opcode::FCmp:
    case Opcode::GetElementPtr:
    case Opcode::BitCast:
    case Opcode::PtrToInt:
    case Opcode::IntToPtr:
    case Opcode::ZExt:
    case Opcode::SExt:
    case Opcode::Trunc:
        return true;
    default:
        return false;
    }
}

}  // namespace

PassChange copy_propagation(Module& module) {
    PassChange change;
    for (const auto& function : module.functions()) {
        if (!function || function->declaration) {
            continue;
        }
        for (const auto& block : function->blocks) {
            if (!block) {
                continue;
            }
            // One table per block: this is local CSE, so a value is available
            // only below its definition in the same block and no dominance
            // reasoning across edges is needed.
            std::map<ExpressionKey, const Value*> available;
            std::map<const Value*, const Value*> replaced;
            for (const auto& instruction : block->instructions) {
                if (!instruction) {
                    continue;
                }
                for (auto*& operand : instruction->operands) {
                    const auto found = replaced.find(operand);
                    if (found != replaced.end()) {
                        operand = const_cast<Value*>(found->second);
                    }
                }
                if (!is_shareable(*instruction)) {
                    continue;
                }
                ExpressionKey key;
                key.opcode = instruction->opcode;
                key.type = instruction->type;
                key.source_type = instruction->source_type;
                key.icmp_predicate = instruction->icmp_predicate;
                key.fcmp_predicate = instruction->fcmp_predicate;
                key.operands.assign(instruction->operands.begin(),
                                    instruction->operands.end());
                const auto existing = available.find(key);
                if (existing == available.end()) {
                    available.emplace(std::move(key), instruction.get());
                    continue;
                }
                replaced[instruction.get()] = existing->second;
            }
            if (replaced.empty()) {
                continue;
            }
            change.instructions_removed += replaced.size();
            const auto dead = std::remove_if(
                block->instructions.begin(), block->instructions.end(),
                [&](const std::unique_ptr<Instruction>& instruction) {
                    return instruction && replaced.count(instruction.get()) != 0;
                });
            block->instructions.erase(dead, block->instructions.end());
        }
    }
    return change;
}

}  // namespace krc::ir
