#pragma once

#include <string>
#include <string_view>

namespace krc::backend {

// `-O0` is straightforward lowering; `-O1` adds the passes plan.md §8.8 lists.
// D-049 makes the distinction narrower than it looks: selection quality — a
// literal operand carried by the instruction instead of a register, a branch
// straight after a comparison — is lowering and belongs to both levels, while
// anything that rewrites the emitted stream after the fact belongs to `-O1`.
enum class OptimizationLevel {
    O0,
    O1,
};

// Removes identity and zero-strength operations from backend assembly. The pass
// is deliberately textual: it only rewrites exact three-operand/immediate forms
// that the KR32 backend emits. This is lowering, so it runs at every level.
[[nodiscard]] std::string peephole(std::string_view assembly);

// Removes an unconditional branch whose target is the label that follows it: the
// branch sets the program counter to where control already falls through to.
// `JMP` (KR32) and `BRA` (KR16) are the forms; a conditional branch is never
// removed, and no label is ever deleted, because a `.Ldbg` label carries a
// line-table entry and any label may be a branch target.
[[nodiscard]] std::string remove_redundant_jumps(std::string_view assembly);

}  // namespace krc::backend
