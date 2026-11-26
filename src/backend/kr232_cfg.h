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

// Removes stores to negative frame slots whose value is never read before the
// next overwrite or the function exit. A call or a load through a pointer may
// observe any frame slot, so either keeps every pending store alive. Stores to
// positive offsets are incoming argument areas and are left alone.
[[nodiscard]] std::string eliminate_dead_frame_stores(std::string_view assembly);

// Removes a frame load when the same slot already lives in the destination
// register on every path into the block. A call, a pointer store or an unknown
// instruction clears the availability map; a frame store records the value
// register, and a move renames a tracked value.
[[nodiscard]] std::string forward_frame_loads_across_blocks(
    std::string_view assembly);

// Drops `mov r8,r13; sub/add r8,#N` when r8 already holds the same frame
// address on every path into the block. Calls and new frames invalidate the
// address; any other write to r8 replaces it.
[[nodiscard]] std::string reuse_frame_addresses_across_blocks(
    std::string_view assembly);

// Replaces `mov r8,r13; sub/add r8,#N` with one `lea r8,[sp+offset]` when the
// stack pointer's displacement from r13 is the same on every path. The KR232
// `lea` has a wide SP-relative form, so this works for frames larger than the
// signed-byte FP displacement.
[[nodiscard]] std::string fold_frame_addresses_to_lea(std::string_view assembly);

}  // namespace krc::backend
