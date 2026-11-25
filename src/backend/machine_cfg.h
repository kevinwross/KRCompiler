// Control flow and liveness over emitted assembly text.
//
// The backends emit text instruction by instruction, and the existing `-O1`
// peepholes inspect adjacent lines. That is enough for local rewrites, but not
// for anything that has to reason about a join or a loop. This module turns an
// emitted function into basic blocks and computes the live-in/live-out sets an
// optimization needs, without committing to one target's mnemonics.
//
// A target supplies `InstructionFlow` for each line: the virtual values it reads
// and writes, the labels it branches to, and whether control can fall through.
// Everything else - block boundaries, successor edges, the fixed point - is
// target-neutral and shared.

#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace krc::backend::machine {

// The data flow of one emitted instruction, as seen by a target adapter.
//
// `uses` are values read before the instruction writes anything; `defs` are the
// values it writes. For a two-address instruction that reads and writes the same
// value, the adapter puts it in both lists. `targets` are labels the instruction
// can transfer control to; `falls_through` says whether the next instruction can
// run after it. A conditional branch has targets and falls through; an
// unconditional branch has targets and does not.
struct InstructionFlow {
    std::vector<int> uses;
    std::vector<int> defs;
    std::vector<std::string> targets;
    bool falls_through = true;
};

// The analyzer is called for instruction lines only. Labels, directives and
// comments are classified by this module.
using InstructionAnalyzer = std::function<InstructionFlow(std::string_view)>;

// One basic block. `instructions` indexes the input lines, so a pass can rewrite
// the original text without a second representation.
struct TextBlock {
    std::string label;
    std::vector<std::size_t> instructions;

    // Upward-exposed uses: values read before the block defines them. `defs` is
    // every value the block writes.
    std::vector<int> uses;
    std::vector<int> defs;

    std::vector<std::size_t> predecessors;
    std::vector<std::size_t> successors;

    std::vector<int> live_in;
    std::vector<int> live_out;
};

struct TextControlFlow {
    std::vector<TextBlock> blocks;
    std::unordered_map<std::string, std::size_t> labels;
    std::size_t entry = 0;

    // Branch targets that no block label defined. A target adapter normally
    // resolves every target; a non-zero count means the input was truncated or
    // the adapter classified a non-branch line as a branch.
    std::size_t unresolved_targets = 0;
};

// Builds blocks from `lines`. `is_label` is not a parameter: a label is a trimmed
// line ending in `:`, and `.Ldbg.` labels do not split a block because they carry
// only line-table entries. A terminator ends a block even when no label follows.
[[nodiscard]] TextControlFlow build_control_flow(
    const std::vector<std::string>& lines, const InstructionAnalyzer& analyze);

// Computes `live_in` and `live_out` for every block with the usual backward
// fixed point: `live_out = union(live_in[successor])` and
// `live_in = uses union (live_out - defs)`. The sets are returned sorted.
void compute_liveness(TextControlFlow& flow);

// True when every successor of every block is a valid block index and the
// successor/predecessor lists agree.
[[nodiscard]] bool control_flow_is_consistent(const TextControlFlow& flow);

}  // namespace krc::backend::machine
