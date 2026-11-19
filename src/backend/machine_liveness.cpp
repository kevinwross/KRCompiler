#include "backend/machine_liveness.h"

#include <algorithm>

namespace krc::backend::machine {

Liveness compute_liveness(const Function& function) {
    Liveness result;

    // Position every instruction in emission order across the blocks. A position is
    // the block's start plus the instruction's index, which is what the recorder
    // produces and what the printer emits in the same order.
    std::map<int, LiveRange> ranges;
    std::vector<std::size_t> call_positions;
    std::vector<int> defined_at;
    std::size_t position = 0;
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
            ++result.instructions;
            if (instruction.opcode == Opcode::Call) {
                call_positions.push_back(position);
            }
            if (instruction.virtual_destination >= 0) {
                ++result.definitions;
                const auto number = instruction.virtual_destination;
                if (static_cast<std::size_t>(number) >= defined_at.size()) {
                    defined_at.resize(static_cast<std::size_t>(number) + 1, -1);
                }
                defined_at[static_cast<std::size_t>(number)] = static_cast<int>(position);
                auto& range = ranges[number];
                range.first = position;
                range.last = position;
            }
            for (const auto source : instruction.virtual_sources) {
                ++result.uses;
                const auto found = ranges.find(source);
                if (found == ranges.end()) {
                    // Read before any definition in this function: a parameter or an
                    // address that exists at entry, which is live from the first
                    // instruction rather than unmodelled.
                    ++result.from_entry;
                    ranges[source] = LiveRange{position, position};
                    continue;
                }
                if (found->second.first > position) {
                    // Defined later in this linear order: a value carried by a loop
                    // back edge, which a linear range cannot describe.
                    ++result.unmodelled;
                }
                ranges[source].last = std::max(ranges[source].last, position);
            }
            ++position;
        }
    }

    // Peak pressure: the most ranges covering one instruction. Ranges are closed
    // intervals, and a value defined and read at the same instruction counts once.
    std::vector<std::pair<std::size_t, int>> events;
    events.reserve(ranges.size() * 2);
    for (const auto& [number, range] : ranges) {
        static_cast<void>(number);
        events.emplace_back(range.first, 1);
        events.emplace_back(range.last + 1, -1);
    }
    std::sort(events.begin(), events.end());
    int live = 0;
    for (const auto& [at, delta] : events) {
        live += delta;
        if (live > 0 && static_cast<std::size_t>(live) > result.peak_pressure) {
            result.peak_pressure = static_cast<std::size_t>(live);
            result.pressure_position = at;
        }
    }

    result.ranges.assign(ranges.size(), LiveRange{});
    result.crosses_call.assign(ranges.size(), false);
    for (const auto& [number, range] : ranges) {
        if (number >= 0) {
            if (static_cast<std::size_t>(number) >= result.ranges.size()) {
                result.ranges.resize(static_cast<std::size_t>(number) + 1);
                result.crosses_call.resize(static_cast<std::size_t>(number) + 1, false);
            }
            result.ranges[static_cast<std::size_t>(number)] = range;
            result.crosses_call[static_cast<std::size_t>(number)] =
                std::any_of(call_positions.begin(), call_positions.end(),
                            [&](std::size_t at) {
                                return range.first < at && at < range.last;
                            });
        }
    }
    return result;
}

Allocation plan_linear_scan(const Liveness& liveness,
                            const std::vector<int>& available_registers) {
    Allocation allocation;
    allocation.registers.assign(liveness.ranges.size(), -1);
    if (available_registers.empty()) {
        allocation.unallocated = liveness.ranges.size();
        return allocation;
    }

    // Values in order of where they start, which is what a linear scan needs. A range
    // with `last < first` is not a range at all and is left unallocated.
    std::vector<std::size_t> order;
    order.reserve(liveness.ranges.size());
    for (std::size_t number = 0; number < liveness.ranges.size(); ++number) {
        const auto& range = liveness.ranges[number];
        const bool crosses_call =
            number < liveness.crosses_call.size() && liveness.crosses_call[number];
        if (range.last >= range.first && !crosses_call) {
            order.push_back(number);
        } else {
            ++allocation.unallocated;
        }
    }
    std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        return liveness.ranges[left].first < liveness.ranges[right].first;
    });

    // `expiry[register_index]` is the position at which the value holding that register
    // dies, so a register is free again at that position.
    std::vector<std::size_t> expiry(available_registers.size(), 0);
    std::vector<bool> in_use(available_registers.size(), false);
    for (const auto number : order) {
        const auto& range = liveness.ranges[number];
        for (std::size_t slot = 0; slot < expiry.size(); ++slot) {
            if (in_use[slot] && expiry[slot] < range.first) {
                in_use[slot] = false;
            }
        }
        bool placed = false;
        for (std::size_t slot = 0; slot < expiry.size(); ++slot) {
            if (in_use[slot]) {
                continue;
            }
            allocation.registers[number] = available_registers[slot];
            expiry[slot] = range.last;
            in_use[slot] = true;
            placed = true;
            break;
        }
        if (!placed) {
            ++allocation.unallocated;
        }
    }
    return allocation;
}

bool interference_is_respected(const Liveness& liveness, const Allocation& allocation) {
    const auto register_of = [&](std::size_t number) {
        return number < allocation.registers.size() ? allocation.registers[number] : -1;
    };
    for (std::size_t left = 0; left < liveness.ranges.size(); ++left) {
        if (register_of(left) < 0) {
            continue;
        }
        const auto& first = liveness.ranges[left];
        for (std::size_t right = left + 1; right < liveness.ranges.size(); ++right) {
            if (register_of(right) < 0 || register_of(right) != register_of(left)) {
                continue;
            }
            const auto& second = liveness.ranges[right];
            // Closed intervals: sharing an instruction is interference.
            if (first.first <= second.last && second.first <= first.last) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace krc::backend::machine
