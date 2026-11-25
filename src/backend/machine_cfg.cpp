#include "backend/machine_cfg.h"

#include <algorithm>
#include <set>
#include <string>

namespace krc::backend::machine {

namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

bool is_label(std::string_view text) {
    return !text.empty() && text.back() == ':' && text.find(' ') == std::string_view::npos;
}

std::string_view label_name(std::string_view text) {
    return text.substr(0, text.size() - 1);
}

void add_successor(TextBlock& block, std::size_t successor) {
    if (std::find(block.successors.begin(), block.successors.end(), successor) ==
        block.successors.end()) {
        block.successors.push_back(successor);
    }
}

void add_values(std::set<int>& destination, const std::vector<int>& values) {
    destination.insert(values.begin(), values.end());
}

}  // namespace

TextControlFlow build_control_flow(const std::vector<std::string>& lines,
                                   const InstructionAnalyzer& analyze) {
    TextControlFlow flow;
    std::vector<std::set<int>> block_uses;
    std::vector<std::set<int>> block_defs;
    std::vector<std::vector<std::string>> block_targets;
    std::vector<bool> block_falls_through;

    int current = -1;
    std::string pending_label;

    const auto begin_block = [&](std::string label) {
        TextBlock block;
        if (label.empty()) {
            label = ".Lcfg." + std::to_string(flow.blocks.size());
        }
        block.label = std::move(label);
        flow.blocks.push_back(std::move(block));
        block_uses.emplace_back();
        block_defs.emplace_back();
        block_targets.emplace_back();
        block_falls_through.push_back(true);
        return static_cast<int>(flow.blocks.size() - 1);
    };

    for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
        const auto text = trim(lines[line_index]);
        if (text.empty() || text.front() == ';' || text.front() == '#') {
            continue;
        }
        if (is_label(text)) {
            const auto name = label_name(text);
            if (name.rfind(".Ldbg.", 0) == 0) {
                continue;
            }
            if (current >= 0 && flow.blocks[static_cast<std::size_t>(current)]
                                    .instructions.empty()) {
                flow.blocks[static_cast<std::size_t>(current)].label =
                    std::string(name);
            } else {
                current = -1;
            }
            pending_label = std::string(name);
            continue;
        }
        if (text.front() == '.') {
            continue;
        }
        if (current < 0) {
            current = begin_block(std::move(pending_label));
            pending_label.clear();
            flow.labels.emplace(flow.blocks[static_cast<std::size_t>(current)].label,
                                static_cast<std::size_t>(current));
        }

        auto& block = flow.blocks[static_cast<std::size_t>(current)];
        block.instructions.push_back(line_index);
        const auto effects = analyze(text);
        for (const auto use : effects.uses) {
            if (!block_defs[static_cast<std::size_t>(current)].contains(use)) {
                block_uses[static_cast<std::size_t>(current)].insert(use);
            }
        }
        for (const auto def : effects.defs) {
            block_defs[static_cast<std::size_t>(current)].insert(def);
        }
        for (const auto& target : effects.targets) {
            block_targets[static_cast<std::size_t>(current)].push_back(target);
        }
        block_falls_through[static_cast<std::size_t>(current)] =
            effects.falls_through;
        if (!effects.falls_through || !effects.targets.empty()) {
            current = -1;
        }
    }

    for (std::size_t index = 0; index < flow.blocks.size(); ++index) {
        auto& block = flow.blocks[index];
        block.uses.assign(block_uses[index].begin(), block_uses[index].end());
        block.defs.assign(block_defs[index].begin(), block_defs[index].end());
        for (const auto& target : block_targets[index]) {
            const auto found = flow.labels.find(target);
            if (found == flow.labels.end()) {
                ++flow.unresolved_targets;
                continue;
            }
            add_successor(block, found->second);
        }
        if (block_falls_through[index] && index + 1 < flow.blocks.size()) {
            add_successor(block, index + 1);
        }
    }

    for (std::size_t index = 0; index < flow.blocks.size(); ++index) {
        for (const auto successor : flow.blocks[index].successors) {
            auto& predecessors = flow.blocks[successor].predecessors;
            if (std::find(predecessors.begin(), predecessors.end(), index) ==
                predecessors.end()) {
                predecessors.push_back(index);
            }
        }
    }
    return flow;
}

void compute_liveness(TextControlFlow& flow) {
    std::vector<std::set<int>> live_in(flow.blocks.size());
    std::vector<std::set<int>> live_out(flow.blocks.size());

    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t index = flow.blocks.size(); index-- > 0;) {
            std::set<int> next_live;
            for (const auto successor : flow.blocks[index].successors) {
                add_values(next_live, flow.blocks[successor].live_in);
            }

            std::set<int> incoming(next_live);
            for (const auto def : flow.blocks[index].defs) {
                incoming.erase(def);
            }
            add_values(incoming, flow.blocks[index].uses);

            if (next_live != live_out[index] || incoming != live_in[index]) {
                live_out[index] = std::move(next_live);
                live_in[index] = std::move(incoming);
                changed = true;

                flow.blocks[index].live_out.assign(live_out[index].begin(),
                                                   live_out[index].end());
                flow.blocks[index].live_in.assign(live_in[index].begin(),
                                                  live_in[index].end());
            }
        }
    }
}

bool control_flow_is_consistent(const TextControlFlow& flow) {
    std::vector<std::vector<std::size_t>> expected_predecessors(flow.blocks.size());
    for (std::size_t index = 0; index < flow.blocks.size(); ++index) {
        for (const auto successor : flow.blocks[index].successors) {
            if (successor >= flow.blocks.size()) {
                return false;
            }
            expected_predecessors[successor].push_back(index);
        }
    }
    for (std::size_t index = 0; index < flow.blocks.size(); ++index) {
        for (const auto predecessor : flow.blocks[index].predecessors) {
            if (predecessor >= flow.blocks.size()) {
                return false;
            }
            if (std::find(expected_predecessors[index].begin(),
                          expected_predecessors[index].end(),
                          predecessor) == expected_predecessors[index].end()) {
                return false;
            }
        }
        if (flow.blocks[index].predecessors.size() !=
            expected_predecessors[index].size()) {
            return false;
        }
    }
    return true;
}

}  // namespace krc::backend::machine
