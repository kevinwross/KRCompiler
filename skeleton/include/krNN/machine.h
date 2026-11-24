#pragma once

#include <cstdint>

namespace krNN {

// The smallest thing a VM tree exports: a machine that can be reset and stepped.
// Replace this with the real machine; keep the shape -- `::core` is what krdbg and
// other consumers link, and they need reset and a way to run one step.
class Machine {
public:
    void reset();
    std::uint64_t program_counter() const { return pc_; }
    void step();

private:
    std::uint64_t pc_ = 0;
};

}  // namespace krNN
