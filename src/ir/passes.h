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

}  // namespace krc::ir
