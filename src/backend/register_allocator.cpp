#include "backend/register_allocator.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace krc::backend {

namespace {

bool is_tracked_value(const ir::Value* value) {
    if (!value) {
        return false;
    }
    switch (value->kind) {
    case ir::ValueKind::Parameter:
    case ir::ValueKind::Instruction:
        return value->type && !ir::is_void(value->type);
    default:
        return false;
    }
}

}  // namespace

bool allocate_registers(const ir::Function& function,
                        const std::vector<std::uint32_t>& allowed_registers,
                        RegisterAllocation& allocation, std::string& error) {
    allocation = RegisterAllocation{};
    if (allowed_registers.empty()) {
        error = "register allocation requires at least one register";
        return false;
    }
    if (function.blocks.empty()) {
        error = "register allocation requires a function body";
        return false;
    }

    std::vector<const ir::Value*> values;
    std::unordered_map<const ir::Value*, std::size_t> ids;
    const auto intern = [&](const ir::Value* value) -> std::size_t {
        if (!is_tracked_value(value)) {
            return static_cast<std::size_t>(-1);
        }
        const auto found = ids.find(value);
        if (found != ids.end()) {
            return found->second;
        }
        const auto id = values.size();
        values.push_back(value);
        ids.emplace(value, id);
        return id;
    };

    for (const auto& parameter : function.parameters) {
        intern(parameter.get());
    }
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block->instructions) {
            intern(instruction.get());
        }
    }

    std::unordered_map<const ir::BasicBlock*, std::size_t> block_ids;
    for (std::size_t index = 0; index < function.blocks.size(); ++index) {
        block_ids.emplace(function.blocks[index].get(), index);
    }

    std::vector<std::unordered_set<std::size_t>> uses(function.blocks.size());
    std::vector<std::unordered_set<std::size_t>> defs(function.blocks.size());
    for (std::size_t index = 0; index < function.blocks.size(); ++index) {
        std::unordered_set<std::size_t> defined_so_far;
        for (const auto& instruction : function.blocks[index]->instructions) {
            for (const auto& operand : instruction->operands) {
                const auto operand_id = intern(operand);
                if (operand_id != static_cast<std::size_t>(-1) &&
                    defined_so_far.find(operand_id) == defined_so_far.end()) {
                    uses[index].insert(operand_id);
                }
            }
            if (is_tracked_value(instruction.get())) {
                const auto defined = ids.at(instruction.get());
                defs[index].insert(defined);
                defined_so_far.insert(defined);
            }
        }
    }

    std::vector<std::unordered_set<std::size_t>> live_in(function.blocks.size());
    std::vector<std::unordered_set<std::size_t>> live_out(function.blocks.size());
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t reverse = 0; reverse < function.blocks.size(); ++reverse) {
            const auto index = function.blocks.size() - 1 - reverse;
            const auto& block = *function.blocks[index];
            std::unordered_set<std::size_t> outgoing;
            if (const auto* terminator = block.terminator()) {
                for (const auto* successor : terminator->successors) {
                    const auto successor_id = block_ids.find(successor);
                    if (successor_id == block_ids.end()) {
                        continue;
                    }
                    outgoing.insert(live_in[successor_id->second].begin(),
                                    live_in[successor_id->second].end());
                }
            }
            auto incoming = uses[index];
            for (const auto value : outgoing) {
                if (defs[index].find(value) == defs[index].end()) {
                    incoming.insert(value);
                }
            }
            if (outgoing != live_out[index]) {
                live_out[index] = std::move(outgoing);
                changed = true;
            }
            if (incoming != live_in[index]) {
                live_in[index] = std::move(incoming);
                changed = true;
            }
        }
    }

    std::vector<std::unordered_set<std::size_t>> interference(values.size());
    std::unordered_map<const ir::Instruction*, std::unordered_set<std::size_t>> live_after;
    const auto add_edge = [&](std::size_t left, std::size_t right) {
        if (left == right || left >= values.size() || right >= values.size()) {
            return;
        }
        interference[left].insert(right);
        interference[right].insert(left);
    };

    for (std::size_t index = 0; index < function.blocks.size(); ++index) {
        std::unordered_set<std::size_t> live = live_out[index];
        const auto& instructions = function.blocks[index]->instructions;
        for (auto iterator = instructions.rbegin(); iterator != instructions.rend();
             ++iterator) {
            const auto& instruction = *iterator;
            live_after.emplace(instruction.get(), live);
            if (is_tracked_value(instruction.get())) {
                live.erase(ids.at(instruction.get()));
            }
            for (const auto& operand : instruction->operands) {
                const auto operand_id = intern(operand);
                if (operand_id != static_cast<std::size_t>(-1)) {
                    live.insert(operand_id);
                }
            }
        }
    }

    for (const auto& block : function.blocks) {
        for (const auto& instruction : block->instructions) {
            if (!is_tracked_value(instruction.get())) {
                continue;
            }
            const auto defined = ids.at(instruction.get());
            const auto found = live_after.find(instruction.get());
            if (found == live_after.end()) {
                continue;
            }
            for (const auto live : found->second) {
                add_edge(defined, live);
            }
            // A definition also interferes with the values the instruction reads.
            // The backend lowers one IR instruction into several machine
            // instructions -- a getelementptr becomes a base load, an index load, a
            // multiply and an add -- so if the result shares a register with an
            // operand, the copy that materialises one of them overwrites the other.
            // That is the shape of the -O1 defect that walked a process table by
            // table + table*48: the index was allocated the register the address
            // computation then copied the base into.
            // A getelementptr is the one opcode whose result must not share a
            // register with anything it reads: the backend lowers it to a base
            // load, an index load into a scratch register, a multiply and an add,
            // so sharing lets the copy that materialises the base overwrite the
            // index -- the -O1 defect that walked a process table by
            // table + table*48. Simple arithmetic needs no such edge: `add dst,
            // dst, src` is a legal encoding, so a blanket rule here costs reuse
            // the allocator's own tests measure.
            if (instruction->opcode == ir::Opcode::GetElementPtr) {
                for (const auto& operand : instruction->operands) {
                    const auto operand_id = intern(operand);
                    if (operand_id != static_cast<std::size_t>(-1)) {
                        add_edge(defined, operand_id);
                    }
                }
            }
        }
    }
    const auto entry = block_ids.find(function.blocks.front().get());
    if (entry != block_ids.end()) {
        const auto& entry_live = live_in[entry->second];
        std::vector<std::size_t> ordered(entry_live.begin(), entry_live.end());
        std::sort(ordered.begin(), ordered.end());
        for (std::size_t left = 0; left < ordered.size(); ++left) {
            for (std::size_t right = left + 1; right < ordered.size(); ++right) {
                add_edge(ordered[left], ordered[right]);
            }
        }
    }

    std::vector<std::size_t> order(values.size());
    for (std::size_t index = 0; index < order.size(); ++index) {
        order[index] = index;
    }
    std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        if (interference[left].size() != interference[right].size()) {
            return interference[left].size() > interference[right].size();
        }
        return left < right;
    });

    std::vector<std::int64_t> colours(values.size(), -1);
    for (const auto node : order) {
        std::unordered_set<std::uint32_t> unavailable;
        for (const auto neighbour : interference[node]) {
            if (colours[neighbour] >= 0) {
                unavailable.insert(static_cast<std::uint32_t>(colours[neighbour]));
            }
        }
        bool assigned = false;
        for (std::size_t slot = 0; slot < allowed_registers.size(); ++slot) {
            if (unavailable.find(static_cast<std::uint32_t>(slot)) == unavailable.end()) {
                colours[node] = static_cast<std::int64_t>(slot);
                assigned = true;
                break;
            }
        }
        if (!assigned) {
            allocation.spilled.insert(values[node]);
        }
    }

    for (std::size_t index = 0; index < values.size(); ++index) {
        if (colours[index] < 0) {
            continue;
        }
        allocation.registers.emplace(values[index],
                                     allowed_registers[static_cast<std::size_t>(colours[index])]);
    }
    return true;
}

void keep_local_allocations(const ir::Function& function,
                            const RegisterAllocation& allocation,
                            const std::unordered_set<const ir::Value*>& call_like,
                            std::unordered_map<const ir::Value*, std::uint32_t>& kept) {
    kept.clear();
    for (const auto& block : function.blocks) {
        if (!block) {
            continue;
        }
        for (std::size_t index = 0; index < block->instructions.size(); ++index) {
            const auto& instruction = block->instructions[index];
            if (!instruction) {
                continue;
            }
            const auto found = allocation.registers.find(instruction.get());
            if (found == allocation.registers.end()) {
                continue;
            }
            // A word-sized scalar: an integer or a pointer. A wide value travels
            // as a register pair and an aggregate is copied word by word.
            if (!instruction->type ||
                (instruction->type->kind != ir::TypeKind::Integer &&
                 instruction->type->kind != ir::TypeKind::Pointer) ||
                instruction->type->bits > 32) {
                continue;
            }
            if (instruction->opcode == ir::Opcode::Phi ||
                instruction->opcode == ir::Opcode::Alloca) {
                continue;
            }
            // Every use must be in the defining block...
            bool local = true;
            for (const auto& other : function.blocks) {
                if (!other) {
                    continue;
                }
                for (const auto& user : other->instructions) {
                    if (!user) {
                        continue;
                    }
                    for (const auto* operand : user->operands) {
                        if (operand == instruction.get() && other.get() != block.get()) {
                            local = false;
                        }
                    }
                }
            }
            if (!local) {
                continue;
            }
            // ...and no call may sit between the definition and its last use,
            // because nothing saves a register across a call on either target.
            // `call_like` adds the operations the target lowers to a call, such
            // as a wide integer division.
            std::size_t last_use = index;
            for (std::size_t user_index = 0; user_index < block->instructions.size();
                 ++user_index) {
                const auto& user = block->instructions[user_index];
                if (!user) {
                    continue;
                }
                for (const auto* operand : user->operands) {
                    if (operand == instruction.get() && user_index > last_use) {
                        last_use = user_index;
                    }
                }
            }
            bool crosses_call = false;
            for (std::size_t between = index; between < last_use; ++between) {
                const auto& user = block->instructions[between];
                if (user && (user->opcode == ir::Opcode::Call ||
                             user->opcode == ir::Opcode::Invoke ||
                             call_like.find(user.get()) != call_like.end())) {
                    crosses_call = true;
                }
            }
            if (crosses_call) {
                continue;
            }
            kept.emplace(instruction.get(), found->second);
        }
    }
}

}  // namespace krc::backend
