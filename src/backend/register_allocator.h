#pragma once

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

}  // namespace krc::backend
