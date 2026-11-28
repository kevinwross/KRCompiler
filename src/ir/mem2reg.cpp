// E6: promote an alloca whose loads and stores span blocks, inserting a `phi` at
// every join the slot is live into.
//
// Its own translation unit because the pass is large enough that assembling it
// edit by edit inside `passes.cpp` broke that file four times; and it is developed
// against `tests/unit/test_mem2reg.cpp` rather than by compiling `libc.c`, because
// earlier attempts took their bugs from a 13,000-line IR dump.
//
// The value model is simple and total: a block-entry value is either a real SSA
// value or a phi that does not exist yet (carried as a block index). Every edge
// value is recorded explicitly during one reverse-postorder walk, so filling a
// phi's incoming list is a lookup rather than an inference.

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ir/passes.h"

namespace krc::ir {

namespace {

constexpr std::size_t kNoPhi = static_cast<std::size_t>(-1);

struct Graph {
    std::unordered_map<const BasicBlock*, std::size_t> index;
    std::vector<const BasicBlock*> blocks;
    std::vector<std::vector<std::size_t>> predecessors;
    std::vector<std::vector<std::size_t>> successors;
    std::vector<std::size_t> reverse_postorder;
};

Graph build_graph(const Function& function) {
    Graph graph;
    for (const auto& block : function.blocks) {
        if (block) {
            graph.index.emplace(block.get(), graph.blocks.size());
            graph.blocks.push_back(block.get());
        }
    }
    const std::size_t count = graph.blocks.size();
    graph.predecessors.resize(count);
    graph.successors.resize(count);
    for (std::size_t block = 0; block < count; ++block) {
        for (const auto& instruction : graph.blocks[block]->instructions) {
            if (!instruction) {
                continue;
            }
            for (const auto* successor : instruction->successors) {
                const auto found = graph.index.find(successor);
                if (found == graph.index.end()) {
                    continue;
                }
                graph.successors[block].push_back(found->second);
                graph.predecessors[found->second].push_back(block);
            }
        }
    }
    // Depth-first postorder, reversed. Every predecessor precedes its successor
    // except across a back edge, and a back edge's target always carries a phi —
    // which is why the walk below may assume a live-in join has one.
    std::vector<char> visited(count, 0);
    std::vector<std::size_t> postorder;
    struct Frame {
        std::size_t block = 0;
        std::size_t next = 0;
    };
    std::vector<Frame> stack;
    if (count != 0) {
        stack.push_back({0, 0});
        visited[0] = 1;
    }
    while (!stack.empty()) {
        Frame& frame = stack.back();
        if (frame.next < graph.successors[frame.block].size()) {
            const std::size_t next = graph.successors[frame.block][frame.next++];
            if (!visited[next]) {
                visited[next] = 1;
                stack.push_back({next, 0});
            }
            continue;
        }
        postorder.push_back(frame.block);
        stack.pop_back();
    }
    graph.reverse_postorder.assign(postorder.rbegin(), postorder.rend());
    return graph;
}

// A source variable is the alloca IRGen named `<variable>.addr`; the backend
// writes a `V` record for it, so its slot must survive (docs/symbols.md). A
// shadowed declaration's alloca carries a unique numeric suffix after the marker
// (`x.addr.4`) and is recorded just the same.
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

bool address_is_private(const Instruction& alloca, const Function& function) {
    for (const auto& block : function.blocks) {
        if (!block) {
            continue;
        }
        for (const auto& instruction : block->instructions) {
            if (!instruction) {
                continue;
            }
            for (const auto* operand : instruction->operands) {
                if (operand != &alloca) {
                    continue;
                }
                const bool is_load = instruction->opcode == Opcode::Load &&
                                     !instruction->operands.empty() &&
                                     instruction->operands[0] == &alloca;
                const bool is_store = instruction->opcode == Opcode::Store &&
                                      instruction->operands.size() > 1 &&
                                      instruction->operands[1] == &alloca;
                if ((!is_load && !is_store) || instruction->is_volatile) {
                    return false;
                }
            }
        }
    }
    return true;
}

struct Incoming {
    std::size_t phi_block = 0;
    std::size_t predecessor = 0;
    const Value* value = nullptr;
    std::size_t from_phi = kNoPhi;
};

// A promotion decided before anything is modified, so a refusal costs nothing.
struct Plan {
    std::unordered_map<const Instruction*, const Value*> loads;
    std::unordered_map<const Instruction*, std::size_t> loads_of_phi;
    std::vector<std::size_t> phi_blocks;
    std::vector<Incoming> incoming;
    std::vector<const Instruction*> dead;
    bool viable = false;
};

Plan plan_promotion(const Graph& graph, const Instruction& alloca) {
    Plan plan;
    const std::size_t count = graph.blocks.size();
    std::vector<char> uses(count, 0);
    std::vector<char> defines(count, 0);
    for (std::size_t block = 0; block < count; ++block) {
        bool stored = false;
        for (const auto& instruction : graph.blocks[block]->instructions) {
            if (!instruction) {
                continue;
            }
            const bool is_store = instruction->opcode == Opcode::Store &&
                                  instruction->operands.size() > 1 &&
                                  instruction->operands[1] == &alloca;
            const bool is_load = instruction->opcode == Opcode::Load &&
                                 !instruction->operands.empty() &&
                                 instruction->operands[0] == &alloca;
            if (is_store) {
                stored = true;
                defines[block] = 1;
            } else if (is_load && !stored) {
                uses[block] = 1;
            }
        }
    }
    // Backward liveness: a join the slot is live into needs a phi.
    std::vector<char> live_in(count, 0);
    std::vector<char> live_out(count, 0);
    bool changed = true;
    std::size_t rounds = 0;
    while (changed && rounds++ <= count + 1) {
        changed = false;
        for (std::size_t reverse = 0; reverse < count; ++reverse) {
            const std::size_t block = count - 1 - reverse;
            char outgoing = 0;
            for (const auto successor : graph.successors[block]) {
                if (live_in[successor]) {
                    outgoing = 1;
                }
            }
            const char incoming = (uses[block] || (outgoing && !defines[block])) ? 1 : 0;
            if (outgoing != live_out[block] || incoming != live_in[block]) {
                live_out[block] = outgoing;
                live_in[block] = incoming;
                changed = true;
            }
        }
    }
    std::vector<char> has_phi(count, 0);
    for (std::size_t block = 0; block < count; ++block) {
        if (live_in[block] && graph.predecessors[block].size() >= 2) {
            has_phi[block] = 1;
            plan.phi_blocks.push_back(block);
        }
    }

    // One reverse-postorder walk: each block starts from what its incoming edges
    // carried and leaves with what its outgoing edges carry.
    std::vector<std::size_t> in_phi(count, kNoPhi);
    std::vector<const Value*> in_value(count, nullptr);
    std::vector<char> reached(count, 0);
    for (const auto block : plan.phi_blocks) {
        in_phi[block] = block;
        // A phi block is reached by construction — its value comes from the phi,
        // not from an edge — and the walk must visit it or its loads go unmapped.
        reached[block] = 1;
    }
    if (!graph.reverse_postorder.empty()) {
        reached[graph.reverse_postorder.front()] = 1;
    }
    std::unordered_map<std::size_t, std::unordered_map<std::size_t, Incoming>> edges;
    for (const auto block : graph.reverse_postorder) {
        if (!reached[block]) {
            continue;
        }
        std::size_t current_phi = in_phi[block];
        const Value* current_value = in_value[block];
        for (const auto& instruction : graph.blocks[block]->instructions) {
            if (!instruction) {
                continue;
            }
            const bool is_store = instruction->opcode == Opcode::Store &&
                                  instruction->operands.size() > 1 &&
                                  instruction->operands[1] == &alloca;
            const bool is_load = instruction->opcode == Opcode::Load &&
                                 !instruction->operands.empty() &&
                                 instruction->operands[0] == &alloca;
            if (is_store) {
                current_phi = kNoPhi;
                current_value = instruction->operands[0];
                continue;
            }
            if (!is_load) {
                continue;
            }
            if (current_phi != kNoPhi) {
                plan.loads_of_phi.emplace(instruction.get(), current_phi);
            } else if (current_value != nullptr) {
                plan.loads.emplace(instruction.get(), current_value);
            } else {
                return plan;  // a read this pass cannot name
            }
        }
        for (const auto successor : graph.successors[block]) {
            if (has_phi[successor]) {
                Incoming edge;
                edge.phi_block = successor;
                edge.predecessor = block;
                edge.value = current_value;
                edge.from_phi = current_phi;
                edges[successor][block] = edge;
                continue;
            }
            if (graph.predecessors[successor].size() == 1) {
                in_phi[successor] = current_phi;
                in_value[successor] = current_value;
                reached[successor] = 1;
            }
        }
    }
    for (const auto phi_block : plan.phi_blocks) {
        const auto found = edges.find(phi_block);
        for (const auto predecessor : graph.predecessors[phi_block]) {
            if (found == edges.end() ||
                found->second.find(predecessor) == found->second.end()) {
                return plan;  // a path no store reaches
            }
            plan.incoming.push_back(found->second.at(predecessor));
        }
    }
    for (std::size_t block = 0; block < count; ++block) {
        for (const auto& instruction : graph.blocks[block]->instructions) {
            if (!instruction) {
                continue;
            }
            const bool is_store = instruction->opcode == Opcode::Store &&
                                  instruction->operands.size() > 1 &&
                                  instruction->operands[1] == &alloca;
            const bool is_load = instruction->opcode == Opcode::Load &&
                                 !instruction->operands.empty() &&
                                 instruction->operands[0] == &alloca;
            if (is_load && plan.loads.count(instruction.get()) == 0 &&
                plan.loads_of_phi.count(instruction.get()) == 0) {
                // A load the walk never reached — a block unreachable from the
                // entry. Erasing it would leave a dangling operand behind.
                return plan;
            }
            if (is_store || is_load) {
                plan.dead.push_back(instruction.get());
            }
        }
    }
    // The object goes too, or the candidate would be found again next time.
    plan.dead.push_back(&alloca);
    plan.viable = true;
    return plan;
}

}  // namespace

PassChange promote_cross_block_allocas(Module& module) {
    PassChange change;
    for (const auto& function : module.functions()) {
        if (!function || function->declaration) {
            continue;
        }
        const auto graph = build_graph(*function);
        std::vector<const Instruction*> candidates;
        for (const auto& block : function->blocks) {
            if (!block) {
                continue;
            }
            for (const auto& instruction : block->instructions) {
                if (instruction && instruction->opcode == Opcode::Alloca &&
                    !instruction->is_volatile && instruction->allocated_type &&
                    !is_recorded_variable(*instruction) &&
                    address_is_private(*instruction, *function)) {
                    candidates.push_back(instruction.get());
                }
            }
        }
        for (const auto* candidate : candidates) {
            const auto plan = plan_promotion(graph, *candidate);
            if (!plan.viable) {
                continue;
            }
            std::unordered_map<std::size_t, Instruction*> phi_of;
            for (const auto phi_block : plan.phi_blocks) {
                auto* join = const_cast<BasicBlock*>(graph.blocks[phi_block]);
                auto phi = std::make_unique<Instruction>();
                phi->kind = ValueKind::Instruction;
                phi->opcode = Opcode::Phi;
                phi->type = candidate->allocated_type;
                phi->parent = join;
                phi->name = "promote" + std::to_string(change.instructions_folded++);
                phi_of.emplace(phi_block, phi.get());
                join->instructions.insert(join->instructions.begin(), std::move(phi));
            }
            // A replacement may name another load being replaced, or a phi, so the
            // chain is followed to a value that survives the rewrite.
            const auto resolve = [&](const Value* value) -> const Value* {
                for (int guard = 0; guard < 16; ++guard) {
                    const auto* as_instruction = dynamic_cast<const Instruction*>(value);
                    if (!as_instruction) {
                        return value;
                    }
                    const auto of_phi = plan.loads_of_phi.find(as_instruction);
                    if (of_phi != plan.loads_of_phi.end()) {
                        value = phi_of.at(of_phi->second);
                        continue;
                    }
                    const auto found = plan.loads.find(as_instruction);
                    if (found == plan.loads.end()) {
                        return value;
                    }
                    value = found->second;
                }
                return value;
            };
            for (const auto& any_block : function->blocks) {
                if (!any_block) {
                    continue;
                }
                for (const auto& instruction : any_block->instructions) {
                    if (!instruction) {
                        continue;
                    }
                    for (auto*& operand : instruction->operands) {
                        const auto* resolved = resolve(operand);
                        if (resolved != operand) {
                            operand = const_cast<Value*>(resolved);
                        }
                    }
                    // A phi created for an earlier candidate can name one of these
                    // loads on an incoming edge.
                    for (auto& incoming : instruction->incoming) {
                        const auto* resolved = resolve(incoming.first);
                        if (resolved != incoming.first) {
                            incoming.first = const_cast<Value*>(resolved);
                        }
                    }
                }
            }
            for (const auto& edge : plan.incoming) {
                auto* phi = phi_of.at(edge.phi_block);
                const Value* value =
                    edge.from_phi != kNoPhi ? phi_of.at(edge.from_phi) : edge.value;
                phi->incoming.push_back(
                    {const_cast<Value*>(resolve(value)),
                     const_cast<BasicBlock*>(graph.blocks[edge.predecessor])});
            }
            for (const auto& any_block : function->blocks) {
                if (!any_block) {
                    continue;
                }
                const auto removed = std::remove_if(
                    any_block->instructions.begin(), any_block->instructions.end(),
                    [&](const std::unique_ptr<Instruction>& instruction) {
                        return instruction &&
                               std::find(plan.dead.begin(), plan.dead.end(),
                                         instruction.get()) != plan.dead.end();
                    });
                any_block->instructions.erase(removed, any_block->instructions.end());
            }
            change.instructions_removed += plan.dead.size();
        }
    }
    return change;
}

PassChange split_critical_edges(Module& module) {
    PassChange change;
    for (const auto& function : module.functions()) {
        if (!function || function->declaration) {
            continue;
        }
        std::vector<const BasicBlock*> blocks;
        blocks.reserve(function->blocks.size());
        for (const auto& block : function->blocks) {
            if (block) {
                blocks.push_back(block.get());
            }
        }
        for (const auto* block : blocks) {
            std::vector<BasicBlock*> successors;
            for (const auto& instruction : block->instructions) {
                if (!instruction) {
                    continue;
                }
                for (auto* successor : instruction->successors) {
                    if (successor) {
                        successors.push_back(successor);
                    }
                }
            }
            if (successors.size() < 2) {
                continue;
            }
            for (auto* successor : successors) {
                std::size_t predecessors = 0;
                for (const auto& other : function->blocks) {
                    if (!other) {
                        continue;
                    }
                    for (const auto& instruction : other->instructions) {
                        if (!instruction) {
                            continue;
                        }
                        for (auto* target : instruction->successors) {
                            if (target == successor) {
                                ++predecessors;
                            }
                        }
                    }
                }
                if (predecessors < 2) {
                    continue;
                }
                auto* split = function->add_block(successor->name + ".crit");
                auto terminator = std::make_unique<Instruction>();
                terminator->kind = ValueKind::Instruction;
                terminator->opcode = Opcode::Br;
                // The verifier requires a type on every instruction, terminators
                // included; a branch's is void (IRBuilder::create_br does the same).
                terminator->type = module.types().void_type();
                terminator->parent = split;
                terminator->successors.push_back(successor);
                split->instructions.push_back(std::move(terminator));
                for (const auto& instruction : block->instructions) {
                    if (!instruction) {
                        continue;
                    }
                    for (auto*& target : instruction->successors) {
                        if (target == successor) {
                            target = split;
                        }
                    }
                }
                for (const auto& instruction : successor->instructions) {
                    if (!instruction || instruction->opcode != Opcode::Phi) {
                        continue;
                    }
                    for (auto& incoming : instruction->incoming) {
                        if (incoming.second == block) {
                            incoming.second = split;
                        }
                    }
                }
                ++change.edges_split;
            }
        }
    }
    return change;
}

}  // namespace krc::ir
