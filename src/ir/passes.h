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

    [[nodiscard]] bool changed() const {
        return instructions_removed != 0 || instructions_folded != 0;
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

}  // namespace krc::ir
