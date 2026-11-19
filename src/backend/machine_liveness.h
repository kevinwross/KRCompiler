// Liveness over the recorded machine form (ADR 0051).
//
// The recording carries a virtual register per IR value: `virtual_destination` on the
// instruction that defines it and `virtual_sources` on each instruction that reads it
// (src/backend/machine_ir.h). This header computes, from that data flow alone, what an
// allocator would be asked to do — the live range of every value and the peak number of
// values live at once.
//
// Nothing here allocates. It exists so the decision to allocate can be taken on a
// measurement rather than on an estimate: if the peak pressure of real functions fits
// the register file, an allocator can be built that never spills, and if it does not,
// the spills are known before the first line of allocator code is written.

#pragma once

#include <cstddef>
#include <map>
#include <vector>

#include "backend/machine_ir.h"

namespace krc::backend::machine {

// One virtual register's extent, as instruction positions in emission order across the
// function's blocks: `first` is the instruction that defines it and `last` the latest
// instruction that reads it. A value read before it is defined indicates a shape this
// linear model does not describe — a loop-carried value or a `phi` — and is recorded in
// `unmodelled` rather than silently given a negative range.
struct LiveRange {
    std::size_t first = 0;
    std::size_t last = 0;
};

struct Liveness {
    std::vector<LiveRange> ranges;        // indexed by virtual register number
    std::size_t peak_pressure = 0;        // most values live at one instruction
    std::size_t pressure_position = 0;    // where that peak occurs
    std::size_t definitions = 0;
    std::size_t uses = 0;
    std::size_t from_entry = 0;           // read but never defined: parameters, addresses
    std::size_t unmodelled = 0;           // defined *later*: loop-carried, needs another model
    std::size_t instructions = 0;
};

// The live ranges of `function`, over its instructions in block order.
[[nodiscard]] Liveness compute_liveness(const Function& function);

}  // namespace krc::backend::machine
