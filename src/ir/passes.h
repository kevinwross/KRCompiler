#pragma once

#include <cstddef>

#include "ir/ir.h"

namespace krc::ir {

// `-O1` passes over typed SSA. plan.md §8.8 lists the set; this file holds the
// ones that exist, and `docs/optimize.md` §7 records the order they land in.
//
// Every pass here preserves the invariants the verifier checks, so the driver
// verifies the module after the pipeline rather than after each pass, and each
// pass reports what it changed so a measurement can attribute the delta.
struct PassChange {
    std::size_t instructions_removed = 0;
    std::size_t instructions_folded = 0;
    // Control-flow edges replaced by a block of their own.
    std::size_t edges_split = 0;

    [[nodiscard]] bool changed() const {
        return instructions_removed != 0 || instructions_folded != 0 ||
               edges_split != 0;
    }
};

// Folds a pure integer operation whose operands are all constants into a constant
// of the operation's own type, then rewrites every use. Only 32- and 64-bit
// results are folded: on a narrower type the backend keeps the value in a whole
// word and truncates at the store, so a folded constant would carry a different
// bit pattern than the sequence it replaces.
//
// Nothing that can trap is folded — no division, no remainder — and a shift is
// folded only when its count is inside the type's width, because the backend
// takes the count modulo the word size instead.
[[nodiscard]] PassChange constant_folding(Module& module);

// E2: local common-subexpression elimination, which is copy propagation over the
// values an expression produces. Two instructions in the **same block** that
// compute the same pure expression on the same operands produce the same value,
// so the second is replaced by the first everywhere and removed.
//
// What it refuses, and why:
//  * anything with a memory effect — `load`, `store`, `alloca`, `call`,
//    `invoke`, atomics. Two loads from one address are not one value: a store
//    may sit between them, and this pass makes no aliasing assumption at all;
//  * `volatile` operations, which §8.4 keeps in order and count;
//  * operations that can trap — division and remainder — because two of them
//    are only interchangeable while neither has trapped;
//  * anything across a block boundary: it is *local* CSE, so dominance is the
//    block's own order and no CFG reasoning is needed.
//
// The debug location of the surviving instruction is the earlier one's, so
// merging cannot move a reported line forward.
[[nodiscard]] PassChange copy_propagation(Module& module);

// Forwards a load from a non-escaping alloca to the value most recently stored
// there. The alloca and its stores stay in place, so a recorded `<variable>.addr`
// keeps the debugger's location; only the redundant loads disappear. A
// single-store alloca whose store is in the entry block forwards across blocks;
// an alloca with several stores forwards inside each block.
[[nodiscard]] PassChange forward_alloca_loads(Module& module);

// Removes stores to a non-escaping temporary alloca that no longer has any
// loads, then removes the alloca. An alloca named `<variable>.addr` is kept: its
// store is the debugger's value even when the program never reads it.
[[nodiscard]] PassChange eliminate_dead_alloca_stores(Module& module);

// E4: single-block alloca promotion. An `alloca` whose address is named by no
// `V` record and whose only uses are a load or a store in its own block is not
// an object the program can observe — it is a temporary the emitter introduced —
// so its loads become plain uses of the last value stored and the round trips
// disappear. This is what turns `store; load; store; load` into SSA uses, and it
// is the change that lets E2 and E3 start to fire.
//
// What it refuses, and why:
//  * an alloca named `<variable>.addr`, which is a source variable with a `V`
//    record: promoting it would leave the debugger pointing at a dead slot;
//  * an address that escapes — a `getelementptr` of it, a store of the pointer,
//    an argument, a return. Only `load` and `store` uses are accepted, and only
//    in the alloca's own block;
//  * a load before any store, because the memory then holds nothing this pass
//    can name;
//  * a `volatile` load or store, which §8.4 keeps in order and count.
[[nodiscard]] PassChange alloca_promotion(Module& module);

// E5: empty-block elimination. A block whose only instruction is an unconditional
// branch is a jump to a jump: every predecessor can branch to its destination
// directly and the block disappears. IRGen leaves one behind after most `if`
// statements (`if.end`) and one for an empty arm (`if.else`), so this removes a
// branch per block.
//
// What it refuses, and why:
//  * the entry block, which has no predecessor to redirect;
//  * a block holding anything but that branch — a landing pad, a `resume` or an
//    `invoke` is never a jump to a jump;
//  * an edge whose source terminator is an `invoke`, whose successor pair is the
//    normal and the unwind path: redirecting either would move an exception edge;
//  * a block a `phi` names as its predecessor, so a later promotion cannot have
//    its incoming edge renamed out from under it.
[[nodiscard]] PassChange empty_block_elimination(Module& module);

// E6 (src/ir/mem2reg.cpp): promote an alloca whose loads and stores span blocks,
// inserting a phi at every join the slot is live into. Refuses a recorded
// <variable>.addr, an escaping address, a volatile access, and a load that no
// store reaches on some path -- all decided before anything is rewritten.
[[nodiscard]] PassChange promote_cross_block_allocas(Module& module);

// Splits every edge whose source has more than one successor and whose destination
// has more than one predecessor, so a phi has a block of its own to write on.
[[nodiscard]] PassChange split_critical_edges(Module& module);

// E3: remove a pure instruction whose result nothing uses, and repeat, because
// removing one may orphan the operands that fed it. The whitelist is the safety
// argument: a candidate computes a value and has no other effect.
//
// What it refuses, and why:
//  * anything with a memory effect — `load`, `store`, `alloca`, `call`, `invoke`
//    — and anything `volatile`, which §8.4 keeps in order and count;
//  * division and remainder, which trap: an unused one still has to execute, and
//    an unused trap is observable;
//  * terminators, which have no result to be unused.
[[nodiscard]] PassChange dead_code_elimination(Module& module);

// Lowering, not optimisation (D-049): a `bitcast` whose operand already has the
// target type computes nothing. Types are canonicalised (ADR 0050), so this is a
// pointer comparison and it removes the cast instead of reintroducing one. Runs at
// -O0 as well as -O1, because it is selection quality rather than a level.
[[nodiscard]] PassChange fold_identity_casts(Module& module);

}  // namespace krc::ir
