#pragma once

#include <string>
#include <string_view>

namespace krc::backend {

// Removes KR232 register definitions whose result is dead along every path:
// `mov rD, ...`, frame loads into an unused rD, and immediate loads into an
// unused rD. The instruction-level liveness is computed over the emitted text,
// so a definition followed by a branch or a join is handled as well as one
// followed by an overwrite in the same block.
//
// Stores, arbitrary memory loads and calls are never removed: a store and a
// call have side effects, and a load through a pointer may fault or observe a
// device. `r0`, `r1`, `r13`, `r14` and `r15` are never treated as removable
// destinations.
[[nodiscard]] std::string eliminate_dead_register_definitions(
    std::string_view assembly);

}  // namespace krc::backend
