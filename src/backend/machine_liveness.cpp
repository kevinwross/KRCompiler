#include "backend/machine_liveness.h"

#include <algorithm>

namespace krc::backend::machine {

Liveness compute_liveness(const Function& function) {
    Liveness result;

    // Position every instruction in emission order across the blocks. A position is
    // the block's start plus the instruction's index, which is what the recorder
    // produces and what the printer emits in the same order.
    std::map<int, LiveRange> ranges;
    std::vector<int> defined_at;
    std::size_t position = 0;
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
            ++result.instructions;
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
    for (const auto& [number, range] : ranges) {
        if (number >= 0) {
            if (static_cast<std::size_t>(number) >= result.ranges.size()) {
                result.ranges.resize(static_cast<std::size_t>(number) + 1);
            }
            result.ranges[static_cast<std::size_t>(number)] = range;
        }
    }
    return result;
}

}  // namespace krc::backend::machine
