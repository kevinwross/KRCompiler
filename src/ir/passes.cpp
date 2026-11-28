#include "ir/passes.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
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
    // A boolean argument arrives as `trunc i32 <literal> to i1`. Folding that
    // truncation lets the call pass the constant directly instead of masking a
    // materialized 0/1 in the backend.
    if (instruction.opcode == Opcode::Trunc && instruction.type->bits == 1 &&
        instruction.operands.size() == 1 && instruction.operands[0] &&
        instruction.operands[0]->type &&
        instruction.operands[0]->type->kind == TypeKind::Integer) {
        const auto value = constant_operand(instruction.operands[0], folded);
        if (!value) {
            return nullptr;
        }
        return module.constant_int(instruction.type,
                                   static_cast<std::int64_t>(
                                       static_cast<std::uint64_t>(*value) & 1u));
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
// writes a `V` record for it (docs/symbols.md), so the address must survive. A
// shadowed declaration gets a unique numeric suffix after the marker
// (`x.addr.4`) and is just as recorded: promoting it would drop the record's slot.
bool is_recorded_variable(const Instruction& alloca) {
    const auto& name = alloca.name;
    const auto marker = name.find(".addr");
    if (marker == std::string::npos) {
        return false;
    }
    const std::string tail = name.substr(marker + 5);
    if (tail.empty()) {
        return true;
    }
    return tail.front() == '.' && tail.size() > 1 &&
           std::all_of(tail.begin() + 1, tail.end(), [](char character) {
               return std::isdigit(static_cast<unsigned char>(character)) != 0;
           });
}

// True when every use of `alloca` is a load or a store in `block`, and none of
// them is volatile: the address neither escapes nor is observed anywhere else.
bool is_promotable(const Instruction& alloca, const BasicBlock& block,
                   const Function& function, bool promote_recorded) {
    if (alloca.opcode != Opcode::Alloca || alloca.is_volatile ||
        !alloca.allocated_type ||
        (!promote_recorded && is_recorded_variable(alloca))) {
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

PassChange forward_alloca_loads(Module& module) {
    PassChange change;
    for (const auto& function : module.functions()) {
        if (!function || function->declaration || function->blocks.empty()) {
            continue;
        }
        std::unordered_set<const Instruction*> candidates;
        for (const auto& block : function->blocks) {
            if (!block) {
                continue;
            }
            for (const auto& instruction : block->instructions) {
                if (!instruction || instruction->opcode != Opcode::Alloca ||
                    instruction->is_volatile || !instruction->allocated_type ||
                    (instruction->allocated_type->kind != TypeKind::Integer &&
                     instruction->allocated_type->kind != TypeKind::Float &&
                     instruction->allocated_type->kind != TypeKind::Pointer)) {
                    continue;
                }
                bool escapes = false;
                for (const auto& other_block : function->blocks) {
                    if (!other_block) {
                        continue;
                    }
                    for (const auto& other : other_block->instructions) {
                        if (!other) {
                            continue;
                        }
                        for (const auto* operand : other->operands) {
                            if (operand != instruction.get()) {
                                continue;
                            }
                            const bool load = other->opcode == Opcode::Load &&
                                              other->operands[0] == instruction.get();
                            const bool store = other->opcode == Opcode::Store &&
                                               other->operands[1] == instruction.get();
                            if ((!load && !store) || other->is_volatile) {
                                escapes = true;
                            }
                        }
                    }
                }
                if (!escapes) {
                    candidates.insert(instruction.get());
                }
            }
        }
        if (candidates.empty()) {
            continue;
        }
        const auto* entry = function->blocks.front().get();
        std::unordered_map<const Instruction*, const Value*> single_store;
        for (const auto* candidate : candidates) {
            std::size_t stores = 0;
            const Value* value = nullptr;
            const BasicBlock* store_block = nullptr;
            for (const auto& block : function->blocks) {
                if (!block) {
                    continue;
                }
                for (std::size_t index = 0; index < block->instructions.size(); ++index) {
                    const auto& instruction = block->instructions[index];
                    if (!instruction || instruction->opcode != Opcode::Store ||
                        instruction->operands.size() < 2 ||
                        instruction->operands[1] != candidate) {
                        continue;
                    }
                    ++stores;
                    value = instruction->operands[0];
                    store_block = block.get();
                }
            }
            const bool computed_unsafe =
                value != nullptr && value->kind == ValueKind::Instruction &&
                value->type != nullptr &&
                value->type->kind == TypeKind::Pointer;
            if (stores == 1 && store_block == entry && !computed_unsafe) {
                single_store[candidate] = value;
            }
        }
        std::unordered_map<const Instruction*, const Value*> replacements;
        const auto candidate_value = [&](const Value* value) -> const Instruction* {
            if (value == nullptr || value->kind != ValueKind::Instruction) {
                return nullptr;
            }
            const auto* instruction = static_cast<const Instruction*>(value);
            return candidates.count(instruction) != 0 ? instruction : nullptr;
        };
        for (const auto& block : function->blocks) {
            if (!block) {
                continue;
            }
            std::unordered_map<const Instruction*, const Value*> current;
            for (std::size_t index = 0; index < block->instructions.size(); ++index) {
                const auto& instruction = block->instructions[index];
                if (!instruction) {
                    continue;
                }
                if (instruction->opcode == Opcode::Store &&
                    instruction->operands.size() >= 2) {
                    if (const auto* alloca = candidate_value(instruction->operands[1])) {
                        const auto* value = instruction->operands[0];
                        const bool computed_unsafe =
                            value != nullptr &&
                            value->kind == ValueKind::Instruction &&
                            value->type != nullptr &&
                            value->type->kind == TypeKind::Pointer;
                        if (!computed_unsafe) {
                            current[alloca] = value;
                        } else {
                            // A computed pointer is not forwarded, so what the slot
                            // holds is no longer known: forget the earlier value
                            // rather than keep forwarding it. Leaving it in place
                            // made `root = insert(root, ...)` read the initializer
                            // on every later iteration, which is how the binarytree
                            // example came back with a height of zero at -O1.
                            current.erase(alloca);
                        }
                    }
                    continue;
                }
                if (instruction->opcode == Opcode::Load &&
                    instruction->operands.size() >= 1) {
                    const auto* alloca = candidate_value(instruction->operands[0]);
                    if (alloca == nullptr) {
                        continue;
                    }
                    const auto global = single_store.find(alloca);
                    if (global != single_store.end() && block.get() != entry &&
                        instruction->type && global->second->type &&
                        instruction->type == global->second->type) {
                        replacements[instruction.get()] = global->second;
                        continue;
                    }
                    const auto found = current.find(alloca);
                    if (found != current.end() && instruction->type &&
                        found->second->type &&
                        instruction->type == found->second->type) {
                        replacements[instruction.get()] = found->second;
                    }
                }
            }
        }
        if (replacements.empty()) {
            continue;
        }
        const auto resolve = [&](const Value* value) {
            for (int step = 0; step < 8 && value != nullptr; ++step) {
                if (value->kind != ValueKind::Instruction) {
                    break;
                }
                const auto found =
                    replacements.find(static_cast<const Instruction*>(value));
                if (found == replacements.end()) {
                    break;
                }
                value = found->second;
            }
            return value;
        };
        for (const auto& block : function->blocks) {
            if (!block) {
                continue;
            }
            for (const auto& instruction : block->instructions) {
                if (!instruction) {
                    continue;
                }
                for (auto*& operand : instruction->operands) {
                    if (operand != nullptr) {
                        operand = const_cast<Value*>(resolve(operand));
                    }
                }
                for (auto& incoming : instruction->incoming) {
                    if (incoming.first != nullptr) {
                        incoming.first = const_cast<Value*>(resolve(incoming.first));
                    }
                }
            }
        }
        for (const auto& block : function->blocks) {
            if (!block) {
                continue;
            }
            const auto removed = std::remove_if(
                block->instructions.begin(), block->instructions.end(),
                [&](const std::unique_ptr<Instruction>& instruction) {
                    return instruction && instruction->opcode == Opcode::Load &&
                           replacements.count(instruction.get()) != 0;
                });
            change.instructions_removed +=
                static_cast<std::size_t>(std::distance(removed, block->instructions.end()));
            block->instructions.erase(removed, block->instructions.end());
        }
    }
    return change;
}

PassChange eliminate_dead_alloca_stores(Module& module) {
    PassChange change;
    for (const auto& function : module.functions()) {
        if (!function || function->declaration) {
            continue;
        }
        std::vector<Instruction*> dead_stores;
        std::vector<Instruction*> dead_allocas;
        for (const auto& block : function->blocks) {
            if (!block) {
                continue;
            }
            for (const auto& alloca : block->instructions) {
                if (!alloca || alloca->opcode != Opcode::Alloca ||
                    alloca->is_volatile) {
                    continue;
                }
                // A recorded source variable keeps its store even when nothing in
                // the program reloads it: the debug record makes the slot
                // observable (including a shadowed declaration's `x.addr.4`).
                if (is_recorded_variable(*alloca)) {
                    continue;
                }
                bool escaped = false;
                bool has_load = false;
                std::vector<Instruction*> stores;
                for (const auto& other_block : function->blocks) {
                    if (!other_block) {
                        continue;
                    }
                    for (const auto& instruction : other_block->instructions) {
                        if (!instruction) {
                            continue;
                        }
                        for (const auto* operand : instruction->operands) {
                            if (operand != alloca.get()) {
                                continue;
                            }
                            const bool load = instruction->opcode == Opcode::Load &&
                                              instruction->operands[0] == alloca.get();
                            const bool store = instruction->opcode == Opcode::Store &&
                                               instruction->operands[1] == alloca.get();
                            if (load && !instruction->is_volatile) {
                                has_load = true;
                            } else if (store && !instruction->is_volatile) {
                                stores.push_back(instruction.get());
                            } else {
                                escaped = true;
                            }
                        }
                    }
                }
                if (escaped || has_load) {
                    continue;
                }
                dead_stores.insert(dead_stores.end(), stores.begin(), stores.end());
                dead_allocas.push_back(alloca.get());
            }
        }
        for (const auto* dead : dead_stores) {
            for (const auto& block : function->blocks) {
                if (!block) {
                    continue;
                }
                const auto removed = std::remove_if(
                    block->instructions.begin(), block->instructions.end(),
                    [&](const std::unique_ptr<Instruction>& instruction) {
                        return instruction.get() == dead;
                    });
                change.instructions_removed += static_cast<std::size_t>(
                    std::distance(removed, block->instructions.end()));
                block->instructions.erase(removed, block->instructions.end());
            }
        }
        for (const auto* dead : dead_allocas) {
            bool used = false;
            for (const auto& block : function->blocks) {
                if (!block) {
                    continue;
                }
                for (const auto& instruction : block->instructions) {
                    if (!instruction) {
                        continue;
                    }
                    for (const auto* operand : instruction->operands) {
                        if (operand == dead) {
                            used = true;
                        }
                    }
                }
            }
            if (used) {
                continue;
            }
            for (const auto& block : function->blocks) {
                if (!block) {
                    continue;
                }
                const auto removed = std::remove_if(
                    block->instructions.begin(), block->instructions.end(),
                    [&](const std::unique_ptr<Instruction>& instruction) {
                        return instruction.get() == dead;
                    });
                change.instructions_removed += static_cast<std::size_t>(
                    std::distance(removed, block->instructions.end()));
                block->instructions.erase(removed, block->instructions.end());
            }
        }
    }
    return change;
}

PassChange alloca_promotion(Module& module, bool promote_recorded) {
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
                    if (instruction &&
                        is_promotable(*instruction, *block, *function,
                                      promote_recorded)) {
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

PassChange fold_identity_casts(Module& module) {
    PassChange change;
    for (const auto& function : module.functions()) {
        if (!function || function->declaration) {
            continue;
        }
        for (const auto& block : function->blocks) {
            if (!block) {
                continue;
            }
            // Types are canonicalised (ADR 0050), so an identity cast is one whose
            // operand's type *is* the result type — a pointer comparison, not a
            // structural walk.
            std::vector<const Instruction*> identity;
            for (const auto& instruction : block->instructions) {
                if (instruction && instruction->opcode == Opcode::BitCast &&
                    instruction->operands.size() == 1 && instruction->operands[0] &&
                    instruction->operands[0]->type == instruction->type) {
                    identity.push_back(instruction.get());
                }
            }
            if (identity.empty()) {
                continue;
            }
            // Every use is replaced before anything is removed, so no operand is
            // ever left pointing at a dead instruction.
            for (const auto& any_block : function->blocks) {
                if (!any_block) {
                    continue;
                }
                for (const auto& instruction : any_block->instructions) {
                    if (!instruction) {
                        continue;
                    }
                    for (auto*& operand : instruction->operands) {
                        const auto found = std::find(identity.begin(), identity.end(), operand);
                        if (found != identity.end()) {
                            operand = const_cast<Value*>((*found)->operands[0]);
                        }
                    }
                    for (auto& incoming : instruction->incoming) {
                        const auto found =
                            std::find(identity.begin(), identity.end(), incoming.first);
                        if (found != identity.end()) {
                            incoming.first = const_cast<Value*>((*found)->operands[0]);
                        }
                    }
                }
            }
            const auto removed = std::remove_if(
                block->instructions.begin(), block->instructions.end(),
                [&](const std::unique_ptr<Instruction>& instruction) {
                    return instruction &&
                           std::find(identity.begin(), identity.end(), instruction.get()) !=
                               identity.end();
                });
            change.instructions_removed +=
                static_cast<std::size_t>(std::distance(removed, block->instructions.end()));
            block->instructions.erase(removed, block->instructions.end());
        }
    }
    return change;
}

}  // namespace krc::ir
