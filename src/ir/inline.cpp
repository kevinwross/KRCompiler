#include "ir/passes.h"

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace krc::ir {

namespace {

bool is_scalar(const TypePtr& type) {
    return type != nullptr && (type->kind == TypeKind::Integer ||
                               type->kind == TypeKind::Pointer ||
                               type->kind == TypeKind::Float);
}

bool is_inlineable(const Function& function) {
    if (function.declaration || !function.type ||
        function.type->kind != TypeKind::Function || function.type->variadic ||
        function.blocks.size() != 1 ||
        function.linkage != Linkage::Internal) {
        return false;
    }
    if (!is_scalar(function.type->return_type) &&
        !(function.type->return_type &&
          function.type->return_type->kind == TypeKind::Void)) {
        return false;
    }
    for (const auto& parameter : function.parameters) {
        if (!is_scalar(parameter->type)) {
            return false;
        }
    }
    const auto& block = function.blocks.front();
    if (!block || !block->terminator() ||
        block->terminator()->opcode != Opcode::Ret) {
        return false;
    }
    if (block->instructions.size() > 12) {
        return false;
    }
    for (const auto& instruction : block->instructions) {
        if (!instruction) {
            continue;
        }
        switch (instruction->opcode) {
        case Opcode::Call:
        case Opcode::Invoke:
        case Opcode::Phi:
        case Opcode::LandingPad:
        case Opcode::Resume:
        case Opcode::VaStart:
        case Opcode::Br:
        case Opcode::CondBr:
        case Opcode::Switch:
        case Opcode::Unreachable:
        case Opcode::Intrinsic:
            return false;
        default:
            break;
        }
    }
    return true;
}

std::size_t call_sites(const Module& module, const Function* callee) {
    std::size_t count = 0;
    for (const auto& function : module.functions()) {
        if (!function || function->declaration) {
            continue;
        }
        for (const auto& block : function->blocks) {
            if (!block) {
                continue;
            }
            for (const auto& instruction : block->instructions) {
                if (instruction && instruction->opcode == Opcode::Call &&
                    !instruction->operands.empty() &&
                    instruction->operands.front() == callee) {
                    ++count;
                }
            }
        }
    }
    return count;
}

std::size_t reference_count(const Module& module, const Function* callee) {
    std::size_t count = 0;
    for (const auto& function : module.functions()) {
        if (!function || function->declaration) {
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
                for (const auto* operand : instruction->operands) {
                    if (operand == callee) {
                        ++count;
                    }
                }
            }
        }
    }
    return count;
}

void replace_uses(Module& module, const Value* from, Value* to) {
    for (const auto& function : module.functions()) {
        if (!function || function->declaration) {
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
                for (auto*& operand : instruction->operands) {
                    if (operand == from) {
                        operand = to;
                    }
                }
                for (auto& incoming : instruction->incoming) {
                    if (incoming.first == from) {
                        incoming.first = to;
                    }
                }
            }
        }
    }
}

bool inline_at(Module& module, BasicBlock& block, std::size_t index,
               const Function& callee) {
    auto* call = block.instructions[index].get();
    if (!call || call->opcode != Opcode::Call ||
        call->operands.size() != callee.parameters.size() + 1) {
        return false;
    }

    std::unordered_map<const Value*, Value*> remap;
    for (std::size_t parameter = 0; parameter < callee.parameters.size();
         ++parameter) {
        remap.emplace(callee.parameters[parameter].get(),
                      call->operands[parameter + 1]);
    }

    const auto& callee_block = callee.blocks.front();
    Value* returned = nullptr;
    for (const auto& instruction : callee_block->instructions) {
        if (!instruction) {
            continue;
        }
        if (instruction->opcode == Opcode::Ret) {
            if (!instruction->operands.empty()) {
                const auto found = remap.find(instruction->operands.front());
                returned = found == remap.end()
                               ? instruction->operands.front()
                               : found->second;
            }
            break;
        }
        auto clone = std::make_unique<Instruction>(*instruction);
        clone->parent = &block;
        clone->name.clear();
        clone->incoming.clear();
        clone->successors.clear();
        clone->switch_cases.clear();
        clone->default_successor = nullptr;
        for (auto*& operand : clone->operands) {
            const auto found = remap.find(operand);
            if (found != remap.end()) {
                operand = found->second;
            }
        }
        remap.emplace(instruction.get(), clone.get());
        block.instructions.insert(block.instructions.begin() + index,
                                  std::move(clone));
        ++index;
    }

    replace_uses(module, call, returned);
    block.instructions.erase(block.instructions.begin() + index);
    return true;
}

}  // namespace

PassChange inline_small_functions(Module& module) {
    PassChange change;
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& function : module.functions()) {
            if (!function || !is_inlineable(*function)) {
                continue;
            }
            if (call_sites(module, function.get()) != 1 ||
                reference_count(module, function.get()) != 1) {
                continue;
            }
            for (const auto& caller : module.functions()) {
                if (!caller || caller->declaration || caller.get() == function.get()) {
                    continue;
                }
                bool inlined = false;
                for (const auto& block : caller->blocks) {
                    if (!block) {
                        continue;
                    }
                    for (std::size_t index = 0; index < block->instructions.size();
                         ++index) {
                        auto* call = block->instructions[index].get();
                        if (!call || call->opcode != Opcode::Call ||
                            call->operands.empty() ||
                            call->operands.front() != function.get()) {
                            continue;
                        }
                        if (inline_at(module, *block, index, *function)) {
                            ++change.functions_inlined;
                            if (call_sites(module, function.get()) == 0) {
                                function->blocks.clear();
                                function->declaration = true;
                            }
                            inlined = true;
                            changed = true;
                            break;
                        }
                    }
                    if (inlined) {
                        break;
                    }
                }
                if (inlined) {
                    break;
                }
            }
        }
    }
    return change;
}

}  // namespace krc::ir
