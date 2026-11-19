#include "ir/passes.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
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

namespace {

// A source variable is the alloca IRGen named `<variable>.addr`; the backend
// writes a `V` record for it (docs/symbols.md), so the address must survive.
bool is_recorded_variable(const Instruction& alloca) {
    const auto& name = alloca.name;
    return name.size() > 5 && name.compare(name.size() - 5, 5, ".addr") == 0;
}

// True when every use of `alloca` is a load or a store in `block`, and none of
// them is volatile: the address neither escapes nor is observed anywhere else.
bool is_promotable(const Instruction& alloca, const BasicBlock& block,
                   const Function& function) {
    if (alloca.opcode != Opcode::Alloca || alloca.is_volatile ||
        !alloca.allocated_type || is_recorded_variable(alloca)) {
        return false;
    }
    for (const auto& candidate_block : function.blocks) {
        if (!candidate_block) {
            continue;
        }
        for (const auto& instruction : candidate_block->instructions) {
            if (!instruction) {
                continue;
            }
            bool uses_address = false;
            for (const auto* operand : instruction->operands) {
                if (operand != &alloca) {
                    continue;
                }
                uses_address = true;
                const bool is_load = instruction->opcode == Opcode::Load &&
                                     instruction->operands[0] == &alloca;
                const bool is_store = instruction->opcode == Opcode::Store &&
                                      instruction->operands[1] == &alloca;
                if ((!is_load && !is_store) || candidate_block.get() != &block ||
                    instruction->is_volatile) {
                    return false;
                }
            }
            static_cast<void>(uses_address);
        }
    }
    return true;
}

}  // namespace

PassChange alloca_promotion(Module& module) {
    PassChange change;
    for (const auto& function : module.functions()) {
        if (!function || function->declaration) {
            continue;
        }
        // One candidate at a time: promoting changes the instruction list, and a
        // restart is simpler to reason about than an iterator that survives
        // erasure. A function has few unrecorded allocas, so the cost is trivial.
        bool promoted_any = true;
        while (promoted_any) {
            promoted_any = false;
            for (const auto& block : function->blocks) {
                if (!block) {
                    continue;
                }
                const Instruction* target = nullptr;
                for (const auto& instruction : block->instructions) {
                    if (instruction && is_promotable(*instruction, *block, *function)) {
                        target = instruction.get();
                        break;
                    }
                }
                if (!target) {
                    continue;
                }
                // Walk the block in order, resolving every load to the value last
                // stored. A load before any store means the memory holds nothing
                // this pass can name, so the candidate is left alone.
                std::map<const Value*, const Value*> loaded;
                std::vector<const Instruction*> dead;
                const Value* current = nullptr;
                bool usable = true;
                for (const auto& instruction : block->instructions) {
                    if (!instruction) {
                        continue;
                    }
                    if (instruction->opcode == Opcode::Load &&
                        instruction->operands[0] == target) {
                        if (!current) {
                            usable = false;
                            break;
                        }
                        loaded[instruction.get()] = current;
                        dead.push_back(instruction.get());
                        continue;
                    }
                    if (instruction->opcode == Opcode::Store &&
                        instruction->operands[1] == target) {
                        const Value* stored = instruction->operands[0];
                        const auto found = loaded.find(stored);
                        if (found != loaded.end()) {
                            stored = found->second;
                        }
                        current = stored;
                        dead.push_back(instruction.get());
                    }
                }
                if (!usable || dead.empty()) {
                    continue;
                }
                // Rewrite the loads' uses across the whole function, because a
                // value loaded here may flow into another block.
                for (const auto& any_block : function->blocks) {
                    if (!any_block) {
                        continue;
                    }
                    for (const auto& instruction : any_block->instructions) {
                        if (!instruction) {
                            continue;
                        }
                        for (auto*& operand : instruction->operands) {
                            const auto found = loaded.find(operand);
                            if (found != loaded.end()) {
                                operand = const_cast<Value*>(found->second);
                            }
                        }
                    }
                }
                dead.push_back(target);
                const auto dead_count = dead.size();
                for (const auto& any_block : function->blocks) {
                    if (!any_block) {
                        continue;
                    }
                    const auto removed = std::remove_if(
                        any_block->instructions.begin(), any_block->instructions.end(),
                        [&](const std::unique_ptr<Instruction>& instruction) {
                            return instruction &&
                                   std::find(dead.begin(), dead.end(), instruction.get()) !=
                                       dead.end();
                        });
                    any_block->instructions.erase(removed, any_block->instructions.end());
                }
                change.instructions_removed += dead_count;
                promoted_any = true;
                break;
            }
        }
    }
    return change;
}

PassChange empty_block_elimination(Module& module) {
    PassChange change;
    for (const auto& function : module.functions()) {
        if (!function || function->declaration || function->blocks.empty()) {
            continue;
        }
        // A chain of empty blocks may collapse to one destination, so the sweep
        // repeats until a pass finds nothing.
        bool removed_any = true;
        while (removed_any) {
            removed_any = false;
            for (std::size_t index = 1; index < function->blocks.size(); ++index) {
                const auto& candidate = function->blocks[index];
                if (!candidate || candidate->instructions.size() != 1) {
                    continue;
                }
                const auto* branch = candidate->instructions.front().get();
                if (!branch || branch->opcode != Opcode::Br ||
                    branch->successors.size() != 1 || !branch->successors.front() ||
                    branch->successors.front() == candidate.get()) {
                    continue;
                }
                auto* destination = branch->successors.front();
                // A phi that names this block as its predecessor is a value tied
                // to the edge; leave the edge alone.
                bool named_by_phi = false;
                for (const auto& block : function->blocks) {
                    if (!block) {
                        continue;
                    }
                    for (const auto& instruction : block->instructions) {
                        if (!instruction || instruction->opcode != Opcode::Phi) {
                            continue;
                        }
                        for (const auto& incoming : instruction->incoming) {
                            if (incoming.second == candidate.get()) {
                                named_by_phi = true;
                            }
                        }
                    }
                }
                if (named_by_phi) {
                    continue;
                }
                // An `invoke` successor is the normal or the unwind path; moving
                // either would relocate an exception edge.
                bool invoked = false;
                for (const auto& block : function->blocks) {
                    if (!block) {
                        continue;
                    }
                    for (const auto& instruction : block->instructions) {
                        if (!instruction || instruction->opcode != Opcode::Invoke) {
                            continue;
                        }
                        for (auto* target : instruction->successors) {
                            if (target == candidate.get()) {
                                invoked = true;
                            }
                        }
                    }
                }
                if (invoked) {
                    continue;
                }
                for (const auto& block : function->blocks) {
                    if (!block) {
                        continue;
                    }
                    for (const auto& instruction : block->instructions) {
                        if (!instruction) {
                            continue;
                        }
                        for (auto*& target : instruction->successors) {
                            if (target == candidate.get()) {
                                target = destination;
                            }
                        }
                    }
                }
                function->blocks.erase(function->blocks.begin() +
                                       static_cast<std::ptrdiff_t>(index));
                ++change.instructions_removed;
                removed_any = true;
                break;
            }
        }
    }
    return change;
}

namespace {

// A pure value computation: no memory effect, no trap, no ordering.
bool is_pure_value(const Instruction& instruction) {
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

PassChange dead_code_elimination(Module& module) {
    PassChange change;
    for (const auto& function : module.functions()) {
        if (!function || function->declaration) {
            continue;
        }
        // Use counts include phi incoming values, which are operands of a kind.
        std::unordered_map<const Value*, std::size_t> uses;
        for (const auto& block : function->blocks) {
            if (!block) {
                continue;
            }
            for (const auto& instruction : block->instructions) {
                if (!instruction) {
                    continue;
                }
                for (const auto* operand : instruction->operands) {
                    if (operand) {
                        ++uses[operand];
                    }
                }
                for (const auto& incoming : instruction->incoming) {
                    if (incoming.first) {
                        ++uses[incoming.first];
                    }
                }
            }
        }
        // Removing one instruction can orphan the ones that fed it, so the sweep
        // repeats until it finds nothing.
        bool removed_any = true;
        while (removed_any) {
            removed_any = false;
            for (const auto& block : function->blocks) {
                if (!block) {
                    continue;
                }
                const auto dead = std::find_if(
                    block->instructions.begin(), block->instructions.end(),
                    [&](const std::unique_ptr<Instruction>& instruction) {
                        if (!instruction || !is_pure_value(*instruction)) {
                            return false;
                        }
                        const auto found = uses.find(instruction.get());
                        return found == uses.end() || found->second == 0;
                    });
                if (dead == block->instructions.end()) {
                    continue;
                }
                for (const auto* operand : (*dead)->operands) {
                    if (!operand) {
                        continue;
                    }
                    const auto found = uses.find(operand);
                    if (found != uses.end() && found->second > 0) {
                        --found->second;
                    }
                }
                block->instructions.erase(dead);
                ++change.instructions_removed;
                removed_any = true;
                break;
            }
        }
    }
    return change;
}

}  // namespace krc::ir
