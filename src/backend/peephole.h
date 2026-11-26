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
    O2,
    Os,
};

// Whether a level runs the optimizing pipeline at all.
[[nodiscard]] constexpr bool optimizes(OptimizationLevel level) {
    return level != OptimizationLevel::O0;
}

// Whether a level runs the pipeline to a second fixed point. Both the
// performance and size tiers repeat, but the second pass is kept only when it
// makes the text smaller, so repetition cannot inflate code.
[[nodiscard]] constexpr bool is_aggressive(OptimizationLevel level) {
    return level == OptimizationLevel::O2 || level == OptimizationLevel::Os;
}

// Removes identity and zero-strength operations from backend assembly. The pass
// is deliberately textual: it only rewrites exact three-operand/immediate forms
// that the KR32 backend emits. This is lowering, so it runs at every level.
[[nodiscard]] std::string peephole(std::string_view assembly);

// Removes a `LOAD Rx, [frame]` that reads back the value `STORE Rx, [frame]`
// just wrote, when only `.Ldbg` line labels sit between them. No other
// instruction, label or branch may intervene.
[[nodiscard]] std::string forward_store_loads(std::string_view assembly);

// Removes `MOV D,S` when the next instruction overwrites D without reading it.
[[nodiscard]] std::string remove_dead_moves(std::string_view assembly);

// Rewrites `PRODUCER Rscratch,...; MOV Rarg,Rscratch` into the producer writing
// Rarg directly, dropping the move. Only single-destination producers are
// rewritten, and Rscratch must not be read again on this path.
[[nodiscard]] std::string coalesce_argument_producers(std::string_view assembly);

// Rewrites `LOAD D,[addr]; MOV E,D` or `LEA D,[addr]; MOV E,D` to the same
// operation with E as its destination, when D is dead after the move.
[[nodiscard]] std::string coalesce_load_moves(std::string_view assembly);

// Rewrites `MOV D,S; STORE V,[D+off]` to `STORE V,[S+off]`, dropping the move
// when D is written again before it is read.
[[nodiscard]] std::string remove_dead_moves_across_blocks(
    std::string_view assembly);

[[nodiscard]] std::string coalesce_store_addresses(std::string_view assembly);

// Folds `MOV D,S` or `ADD D,S,#off`, followed only by constant moves that do
// not touch S or D, into `LOAD/STORE ..., [S+off]`. The setup is removed only
// when D is overwritten before it is read on the fallthrough path.
[[nodiscard]] std::string fold_address_arithmetic(std::string_view assembly);

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
