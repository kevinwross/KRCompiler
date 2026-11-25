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

// Removes a `LOAD Rx, [frame]` that reads back the value `STORE Rx, [frame]`
// just wrote, when only `.Ldbg` line labels sit between them. No other
// instruction, label or branch may intervene.
[[nodiscard]] std::string forward_store_loads(std::string_view assembly);

// Rewrites `MOV D,S; STORE V,[D+off]` to `STORE V,[S+off]`, dropping the move
// when D is written again before it is read.
[[nodiscard]] std::string coalesce_store_addresses(std::string_view assembly);

// Folds `MOV D, S; OP D, D, X` into `OP D, S, X`, for the two-address integer
// operations the emitter produces after a move into the result register.
[[nodiscard]] std::string combine_move_arithmetic(std::string_view assembly);

// Replaces `MOV R0, Rn; STORE R0, [frame]` with `STORE Rn, [frame]` when R0 is
// written again before it is read, so the move has no other consumer.
[[nodiscard]] std::string coalesce_store_sources(std::string_view assembly);

// Removes a move whose destination already holds the source's value, within a
// straight-line run. Register equalities are tracked as value ids; labels,
// branches, calls and unknown instructions reset them, so no fact crosses a
// control-flow edge or a clobber.
[[nodiscard]] std::string propagate_copies(std::string_view assembly);

// Removes an unconditional branch whose target is the label that follows it: the
// branch sets the program counter to where control already falls through to.
// `JMP` (KR32) and `BRA` (KR16) are the forms; a conditional branch is never
// removed, and no label is ever deleted, because a `.Ldbg` label carries a
// line-table entry and any label may be a branch target.
[[nodiscard]] std::string remove_redundant_jumps(std::string_view assembly);

}  // namespace krc::backend
