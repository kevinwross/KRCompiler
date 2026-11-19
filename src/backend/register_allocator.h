#pragma once

// NOT WIRED IN, deliberately. `allocate_registers` has no caller in the compiler: both
// backends emit assembly from a frame layout, so every value is memory-resident by
// construction and this pass is built and unit-tested but never run. It stays as the
// starting point for register allocation rather than being deleted, and it is recorded
// here so its presence is not read as evidence that locals are register-allocated.
// Wiring it changes how every value in every function is emitted on two targets, which
// is a feature with its own gate rather than a change to fold into another one.
// See implementation.md, 2026-11-17.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ir/ir.h"

namespace krc::backend {

struct RegisterAllocation {
    std::unordered_map<const ir::Value*, std::uint32_t> registers;
    std::unordered_set<const ir::Value*> spilled;
};

// Builds per-instruction liveness, an interference graph, and greedily colours
// it in degree order. `allowed_registers` is the ordered physical register set
// available to this function.
[[nodiscard]] bool allocate_registers(const ir::Function& function,
                                      const std::vector<std::uint32_t>& allowed_registers,
                                      RegisterAllocation& allocation, std::string& error);

// The subset of an allocation a backend can honour today, and why it is a subset:
// `allocate_registers` computes liveness per block and models no call clobbering,
// so a value keeps its register only when its definition and every use sit in one
// block with no call between them, where no cross-block or call reasoning is
// needed at all. Word-sized integers and pointers qualify; a wide value travels
// as a register pair and an aggregate is copied word by word, so neither does.
//
// The KR32 backend derives its candidate set this way, so the safety argument
// lives here rather than being restated in an emitter. KR16 does not use it: that
// target has one spare register, a two-operand ALU and four scratch registers
// already spoken for, so an allocated value cannot be written in place without
// first reloading its operands through scratch registers — four instructions
// either way. That is recorded as a measurement in `implementation.md`, not as an
// assumption, per ADR 0049 point 4.
void keep_local_allocations(const ir::Function& function,
                            const RegisterAllocation& allocation,
                            std::unordered_map<const ir::Value*, std::uint32_t>& kept);
}  // namespace krc::backend
