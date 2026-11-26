#include "backend/kr232_cfg.h"

#include <cctype>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "backend/machine_cfg.h"

namespace krc::backend {

namespace {

using machine::InstructionFlow;

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

std::string_view without_comment(std::string_view text) {
    const auto comment = text.find(';');
    return comment == std::string_view::npos ? text : text.substr(0, comment);
}

std::string_view mnemonic_of(std::string_view line) {
    const auto space = line.find_first_of(" \t");
    return space == std::string_view::npos ? line : line.substr(0, space);
}

std::vector<std::string_view> operands_of(std::string_view line) {
    std::vector<std::string_view> operands;
    const auto space = line.find_first_of(" \t");
    if (space == std::string_view::npos) {
        return operands;
    }
    auto rest = line.substr(space + 1);
    while (true) {
        const auto comma = rest.find(',');
        operands.push_back(trim(comma == std::string_view::npos
                                    ? rest
                                    : rest.substr(0, comma)));
        if (comma == std::string_view::npos) {
            break;
        }
        rest = rest.substr(comma + 1);
    }
    return operands;
}

bool parse_register_operand(std::string_view text, int& number) {
    text = trim(text);
    if (text.size() < 2 || text.front() != 'r') {
        return false;
    }
    int value = 0;
    for (std::size_t index = 1; index < text.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(text[index]))) {
            return false;
        }
        value = value * 10 + (text[index] - '0');
    }
    number = value;
    return true;
}

std::vector<int> registers_in(std::string_view text) {
    std::vector<int> registers;
    for (std::size_t index = 0; index < text.size();) {
        if (text[index] != 'r' ||
            (index > 0 && std::isalnum(static_cast<unsigned char>(text[index - 1])))) {
            ++index;
            continue;
        }
        std::size_t end = index + 1;
        int value = 0;
        while (end < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[end]))) {
            value = value * 10 + (text[end] - '0');
            ++end;
        }
        if (end > index + 1) {
            registers.push_back(value);
            index = end;
        } else {
            ++index;
        }
    }
    return registers;
}

bool is_load(std::string_view mnemonic) {
    return mnemonic == "ld" || mnemonic == "ldbu" || mnemonic == "ldbs" ||
           mnemonic == "ldhu" || mnemonic == "ldhs";
}

bool is_arithmetic(std::string_view mnemonic) {
    return mnemonic == "add" || mnemonic == "sub" || mnemonic == "mul" ||
           mnemonic == "div" || mnemonic == "divs" || mnemonic == "mod" ||
           mnemonic == "mods" || mnemonic == "and" || mnemonic == "or" ||
           mnemonic == "xor" || mnemonic == "shl" || mnemonic == "shr" ||
           mnemonic == "sar" || mnemonic == "neg" || mnemonic == "not";
}

std::vector<int> all_registers() {
    std::vector<int> registers;
    for (int register_number = 0; register_number < 16; ++register_number) {
        registers.push_back(register_number);
    }
    return registers;
}

InstructionFlow analyze(std::string_view raw) {
    InstructionFlow flow;
    const auto line = trim(without_comment(raw));
    if (line.empty()) {
        return flow;
    }
    const auto mnemonic = mnemonic_of(line);
    const auto operands = operands_of(line);

    if (mnemonic == "mov") {
        int destination = 0;
        if (!operands.empty() && parse_register_operand(operands[0], destination)) {
            flow.defs.push_back(destination);
        }
        if (operands.size() > 1) {
            const auto sources = registers_in(operands[1]);
            flow.uses.insert(flow.uses.end(), sources.begin(), sources.end());
        }
        return flow;
    }
    if (is_load(mnemonic)) {
        int destination = 0;
        if (!operands.empty() && parse_register_operand(operands[0], destination)) {
            flow.defs.push_back(destination);
        }
        if (operands.size() > 1) {
            const auto sources = registers_in(operands[1]);
            flow.uses.insert(flow.uses.end(), sources.begin(), sources.end());
        }
        return flow;
    }
    if (mnemonic == "st" || mnemonic == "stb" || mnemonic == "stw") {
        const auto sources = registers_in(line.substr(mnemonic.size()));
        flow.uses.insert(flow.uses.end(), sources.begin(), sources.end());
        return flow;
    }
    if (is_arithmetic(mnemonic)) {
        int destination = 0;
        if (!operands.empty() && parse_register_operand(operands[0], destination)) {
            flow.defs.push_back(destination);
            flow.uses.push_back(destination);
        }
        const auto sources = registers_in(line.substr(mnemonic.size()));
        flow.uses.insert(flow.uses.end(), sources.begin(), sources.end());
        return flow;
    }
    if (mnemonic == "cmp") {
        const auto sources = registers_in(line.substr(mnemonic.size()));
        flow.uses.insert(flow.uses.end(), sources.begin(), sources.end());
        return flow;
    }
    if (mnemonic == "push") {
        const auto sources = registers_in(line.substr(mnemonic.size()));
        flow.uses.insert(flow.uses.end(), sources.begin(), sources.end());
        return flow;
    }
    if (mnemonic == "pop") {
        int destination = 0;
        if (!operands.empty() && parse_register_operand(operands[0], destination)) {
            flow.defs.push_back(destination);
        }
        return flow;
    }
    if (mnemonic == "call") {
        flow.uses = all_registers();
        return flow;
    }
    if (mnemonic == "ret") {
        flow.uses.push_back(2);
        flow.falls_through = false;
        return flow;
    }
    if (mnemonic == "halt") {
        flow.falls_through = false;
        return flow;
    }
    if (mnemonic == "jmp" || mnemonic == "bra") {
        if (!operands.empty()) {
            int target_register = 0;
            const auto target = trim(operands[0]);
            if (parse_register_operand(target, target_register) ||
                (!target.empty() && target.front() == '[')) {
                // An indirect jump is a tail call: it reads the call target and
                // every argument register, and control does not fall through.
                flow.uses = all_registers();
            } else {
                flow.targets.push_back(std::string(target));
            }
        }
        flow.falls_through = false;
        return flow;
    }
    if (mnemonic.size() > 1 && mnemonic.front() == 'b') {
        if (!operands.empty()) {
            flow.targets.push_back(std::string(operands[0]));
        }
        return flow;
    }
    if (mnemonic == "enter" || mnemonic == "leave" || mnemonic == "nop") {
        return flow;
    }
    flow.uses = all_registers();
    return flow;
}

constexpr int kAnyFrameSlot = -1000000;

struct FrameAddressState {
    bool valid = false;
    int offset = 0;
    bool operator==(const FrameAddressState&) const = default;
};

bool parse_frame_slot(std::string_view address, int& slot) {
    address = trim(address);
    if (address.size() < 5 || address.rfind("[r13", 0) != 0) {
        return false;
    }
    auto body = address.substr(4);
    if (!body.empty() && body.front() == ' ') {
        body = trim(body);
    }
    if (body.size() < 2 || body.back() != ']') {
        return false;
    }
    body = body.substr(0, body.size() - 1);
    char* stop = nullptr;
    const std::string body_value(body);
    const long value = std::strtol(body_value.c_str(), &stop, 0);
    if (stop == nullptr || *stop != '\0' || value >= 0) {
        return false;
    }
    slot = static_cast<int>(value);
    return true;
}

InstructionFlow analyze_slots(std::string_view raw) {
    InstructionFlow flow;
    const auto line = trim(without_comment(raw));
    if (line.empty()) {
        return flow;
    }
    const auto mnemonic = mnemonic_of(line);
    const auto operands = operands_of(line);
    const auto is_frame_load = [&](std::size_t address_operand) {
        if (operands.size() <= address_operand) {
            return false;
        }
        int slot = 0;
        if (parse_frame_slot(operands[address_operand], slot)) {
            flow.uses.push_back(slot);
            return true;
        }
        const auto address = trim(operands[address_operand]);
        if (!address.empty() && address.front() == '#') {
            return true;
        }
        flow.uses.push_back(kAnyFrameSlot);
        return true;
    };
    if (mnemonic == "st" || mnemonic == "stb" || mnemonic == "stw") {
        int slot = 0;
        if (!operands.empty() && parse_frame_slot(operands[0], slot)) {
            flow.defs.push_back(slot);
        }
        return flow;
    }
    if (is_load(mnemonic)) {
        static_cast<void>(is_frame_load(1));
        return flow;
    }
    if (mnemonic == "call") {
        flow.uses.push_back(kAnyFrameSlot);
        return flow;
    }
    if (mnemonic == "ret" || mnemonic == "halt" || mnemonic == "jmp" ||
        mnemonic == "bra" || (mnemonic.size() > 1 && mnemonic.front() == 'b') ||
        mnemonic == "enter" || mnemonic == "leave" || mnemonic == "nop" ||
        mnemonic == "mov" || mnemonic == "cmp" || mnemonic == "push" ||
        mnemonic == "pop" || is_arithmetic(mnemonic)) {
        return flow;
    }
    flow.uses.push_back(kAnyFrameSlot);
    return flow;
}

bool is_removable_definition(std::string_view line, int& destination) {    const auto text = trim(without_comment(line));
    const auto mnemonic = mnemonic_of(text);
    const auto operands = operands_of(text);
    if (operands.empty() || !parse_register_operand(operands[0], destination)) {
        return false;
    }
    if (destination == 0 || destination == 1 || destination == 13 ||
        destination == 14 || destination == 15) {
        return false;
    }
    if (mnemonic == "mov") {
        return true;
    }
    if (!is_load(mnemonic) || operands.size() < 2) {
        return false;
    }
    const auto address = trim(operands[1]);
    if (!address.empty() && address.front() == '#') {
        return true;
    }
    return address.rfind("[r13", 0) == 0 || address.rfind("[sp", 0) == 0;
}

}  // namespace

std::string eliminate_dead_register_definitions(std::string_view assembly) {
    std::vector<std::string> lines;
    {
        std::string line;
        std::string text(assembly);
        std::size_t start = 0;
        while (start <= text.size()) {
            const auto end = text.find('\n', start);
            line = text.substr(start, end == std::string::npos
                                          ? std::string::npos
                                          : end - start);
            lines.push_back(line);
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
    }

    auto flow = machine::build_control_flow(lines, analyze);
    machine::compute_liveness(flow);

    std::vector<bool> dead(lines.size(), false);
    for (const auto& block : flow.blocks) {
        std::set<int> live(block.live_out.begin(), block.live_out.end());
        for (auto position = block.instructions.size(); position-- > 0;) {
            const auto line_index = block.instructions[position];
            const auto line = lines[line_index];
            const auto effects = analyze(line);
            int destination = -1;
            if (is_removable_definition(line, destination) &&
                live.find(destination) == live.end()) {
                dead[line_index] = true;
                continue;
            }
            for (const auto def : effects.defs) {
                live.erase(def);
            }
            for (const auto use : effects.uses) {
                live.insert(use);
            }
        }
    }

    std::string result;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (dead[index]) {
            continue;
        }
        result += lines[index];
        result += '\n';
    }
    return result;
}

bool parse_immediate_operand(std::string_view text, int& value) {
    text = trim(text);
    if (text.empty() || text.front() != '#') {
        return false;
    }
    char* stop = nullptr;
    const std::string immediate(text.substr(1));
    const long parsed = std::strtol(immediate.c_str(), &stop, 0);
    if (stop == nullptr || *stop != '\0') {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

bool register_destination(std::string_view line, int& destination) {    const auto text = trim(without_comment(line));
    const auto mnemonic = mnemonic_of(text);
    const auto operands = operands_of(text);
    if (operands.empty()) {
        return false;
    }
    if (mnemonic != "mov" && !is_load(mnemonic) && !is_arithmetic(mnemonic) &&
        mnemonic != "pop") {
        return false;
    }
    return parse_register_operand(operands[0], destination);
}

std::string eliminate_local_dead_frame_stores(std::string_view assembly) {
    std::vector<std::string> lines;
    {
        std::string text(assembly);
        std::size_t start = 0;
        while (start <= text.size()) {
            const auto end = text.find('\n', start);
            lines.push_back(text.substr(start, end == std::string::npos
                                                   ? std::string::npos
                                                   : end - start));
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
    }
    std::vector<bool> dead(lines.size(), false);
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto line = trim(without_comment(lines[index]));
        const auto mnemonic = mnemonic_of(line);
        if (mnemonic != "st" && mnemonic != "stb" && mnemonic != "stw") {
            continue;
        }
        const auto operands = operands_of(line);
        int slot = 0;
        if (operands.empty() || !parse_frame_slot(operands[0], slot)) {
            continue;
        }
        for (std::size_t next = index + 1; next < lines.size(); ++next) {
            const auto candidate = trim(without_comment(lines[next]));
            if (candidate.empty() || candidate.front() == ';' ||
                (candidate.back() == ':' &&
                 candidate.rfind(".Ldbg.", 0) == 0)) {
                continue;
            }
            if (candidate.back() == ':') {
                break;
            }
            const auto next_mnemonic = mnemonic_of(candidate);
            if (next_mnemonic == "call" || next_mnemonic == "ret" ||
                next_mnemonic == "halt" || next_mnemonic == "enter" ||
                next_mnemonic == "leave" || next_mnemonic == "jmp" ||
                next_mnemonic == "bra" ||
                (next_mnemonic.size() > 1 && next_mnemonic.front() == 'b')) {
                break;
            }
            const auto next_operands = operands_of(candidate);
            if (next_mnemonic == "st" || next_mnemonic == "stb" ||
                next_mnemonic == "stw") {
                int next_slot = 0;
                if (!next_operands.empty() &&
                    parse_frame_slot(next_operands[0], next_slot)) {
                    if (next_slot == slot) {
                        dead[index] = true;
                        break;
                    }
                    continue;
                }
                // A store through a pointer cannot observe the pending slot.
                continue;
            }
            if (is_load(next_mnemonic)) {
                int next_slot = 0;
                if (next_operands.size() > 1 &&
                    parse_frame_slot(next_operands[1], next_slot)) {
                    if (next_slot == slot) {
                        break;
                    }
                    continue;
                }
                const auto address =
                    next_operands.size() > 1 ? trim(next_operands[1])
                                             : std::string_view{};
                if (!address.empty() && address.front() == '#') {
                    continue;
                }
                break;
            }
        }
    }
    std::string result;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (dead[index]) {
            continue;
        }
        result += lines[index];
        result += '\n';
    }
    return result;
}

std::string eliminate_dead_frame_stores(std::string_view assembly) {
    std::vector<std::string> lines;
    {
        std::string text(assembly);
        std::size_t start = 0;
        while (start <= text.size()) {
            const auto end = text.find('\n', start);
            lines.push_back(text.substr(start, end == std::string::npos
                                                   ? std::string::npos
                                                   : end - start));
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
    }

    auto flow = machine::build_control_flow(lines, analyze_slots);
    machine::compute_liveness(flow);

    std::vector<bool> dead(lines.size(), false);
    for (const auto& block : flow.blocks) {
        std::set<int> live(block.live_out.begin(), block.live_out.end());
        for (auto position = block.instructions.size(); position-- > 0;) {
            const auto line_index = block.instructions[position];
            const auto line = trim(without_comment(lines[line_index]));
            const auto mnemonic = mnemonic_of(line);
            const auto operands = operands_of(line);
            const bool is_store = mnemonic == "st" || mnemonic == "stb" ||
                                  mnemonic == "stw";
            int slot = 0;
            if (is_store && !operands.empty() &&
                parse_frame_slot(operands[0], slot) &&
                live.find(slot) == live.end() &&
                live.find(kAnyFrameSlot) == live.end()) {
                dead[line_index] = true;
                continue;
            }
            const auto effects = analyze_slots(line);
            for (const auto def : effects.defs) {
                live.erase(def);
            }
            for (const auto use : effects.uses) {
                live.insert(use);
            }
        }
    }

    std::string result;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (dead[index]) {
            continue;
        }
        result += lines[index];
        result += '\n';
    }
    return result;
}

std::string forward_frame_loads_across_blocks(std::string_view assembly) {
    std::vector<std::string> lines;
    {
        std::string text(assembly);
        std::size_t start = 0;
        while (start <= text.size()) {
            const auto end = text.find('\n', start);
            lines.push_back(text.substr(start, end == std::string::npos
                                                   ? std::string::npos
                                                   : end - start));
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
    }

    auto flow = machine::build_control_flow(lines, analyze);
    using State = std::map<int, int>;
    const auto invalidate = [](State& state, int register_number) {
        for (auto entry = state.begin(); entry != state.end();) {
            if (entry->second == register_number) {
                entry = state.erase(entry);
            } else {
                ++entry;
            }
        }
    };

    const auto transfer = [&](const machine::TextBlock& block, State state,
                              std::vector<bool>* dead) {
        for (const auto line_index : block.instructions) {
            const auto line = trim(without_comment(lines[line_index]));
            const auto mnemonic = mnemonic_of(line);
            const auto operands = operands_of(line);
            if (mnemonic == "st" || mnemonic == "stb" || mnemonic == "stw") {
                int slot = 0;
                int value = 0;
                if (!operands.empty() && parse_frame_slot(operands[0], slot) &&
                    operands.size() > 1 &&
                    parse_register_operand(operands[1], value)) {
                    state[slot] = value;
                } else {
                    // A store through a pointer may alias any frame slot.
                    state.clear();
                }
                continue;
            }
            if (is_load(mnemonic)) {
                int destination = 0;
                if (operands.empty() ||
                    !parse_register_operand(operands[0], destination)) {
                    state.clear();
                    continue;
                }
                int slot = 0;
                if (operands.size() > 1 && parse_frame_slot(operands[1], slot)) {
                    const auto found = state.find(slot);
                    if (found != state.end() && found->second == destination) {
                        if (dead != nullptr) {
                            (*dead)[line_index] = true;
                        }
                    } else {
                        invalidate(state, destination);
                        state[slot] = destination;
                    }
                } else {
                    invalidate(state, destination);
                }
                continue;
            }
            if (mnemonic == "mov") {
                int destination = 0;
                int source = 0;
                if (operands.size() > 1 &&
                    parse_register_operand(operands[0], destination) &&
                    parse_register_operand(operands[1], source)) {
                    invalidate(state, destination);
                    for (auto& entry : state) {
                        if (entry.second == source) {
                            entry.second = destination;
                        }
                    }
                } else if (!operands.empty() &&
                           parse_register_operand(operands[0], destination)) {
                    invalidate(state, destination);
                }
                continue;
            }
            if (mnemonic == "call" || mnemonic == "enter" ||
                mnemonic == "leave") {
                state.clear();
                continue;
            }
            int destination = 0;
            if (register_destination(line, destination)) {
                invalidate(state, destination);
                continue;
            }
            if (mnemonic == "ret" || mnemonic == "halt" || mnemonic == "jmp" ||
                mnemonic == "bra" ||
                (mnemonic.size() > 1 && mnemonic.front() == 'b') ||
                mnemonic == "cmp" || mnemonic == "push" || mnemonic == "nop") {
                continue;
            }
            state.clear();
        }
        return state;
    };

    std::vector<std::optional<State>> entries(flow.blocks.size());
    std::vector<std::optional<State>> exits(flow.blocks.size());
    entries[0] = State{};
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t index = 0; index < flow.blocks.size(); ++index) {
            State entry;
            if (index != 0) {
                std::optional<State> joined;
                bool ready = true;
                for (const auto predecessor : flow.blocks[index].predecessors) {
                    if (!exits[predecessor]) {
                        ready = false;
                        break;
                    }
                    if (!joined) {
                        joined = *exits[predecessor];
                        continue;
                    }
                    for (auto candidate = joined->begin();
                         candidate != joined->end();) {
                        const auto found = exits[predecessor]->find(candidate->first);
                        if (found == exits[predecessor]->end() ||
                            found->second != candidate->second) {
                            candidate = joined->erase(candidate);
                        } else {
                            ++candidate;
                        }
                    }
                }
                if (!ready) {
                    continue;
                }
                entry = joined.value_or(State{});
            }
            const auto exit = transfer(flow.blocks[index], entry, nullptr);
            if (!entries[index] || *entries[index] != entry || !exits[index] ||
                *exits[index] != exit) {
                entries[index] = entry;
                exits[index] = exit;
                changed = true;
            }
        }
    }

    std::vector<bool> dead(lines.size(), false);
    for (std::size_t index = 0; index < flow.blocks.size(); ++index) {
        State state = entries[index].value_or(State{});
        static_cast<void>(transfer(flow.blocks[index], std::move(state), &dead));
    }

    std::string result;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (dead[index]) {
            continue;
        }
        result += lines[index];
        result += '\n';
    }
    return result;
}

std::string reuse_frame_addresses_across_blocks(std::string_view assembly) {
    std::vector<std::string> lines;
    {
        std::string text(assembly);
        std::size_t start = 0;
        while (start <= text.size()) {
            const auto end = text.find('\n', start);
            lines.push_back(text.substr(start, end == std::string::npos
                                                   ? std::string::npos
                                                   : end - start));
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
    }

    auto flow = machine::build_control_flow(lines, analyze);
    const auto transfer = [&](const machine::TextBlock& block,
                              FrameAddressState state, std::vector<bool>* dead) {
        for (std::size_t position = 0; position < block.instructions.size();
             ++position) {
            const auto line_index = block.instructions[position];
            const auto line = trim(without_comment(lines[line_index]));
            const auto mnemonic = mnemonic_of(line);
            const auto operands = operands_of(line);
            if (mnemonic == "mov" && operands.size() > 1 &&
                operands[0] == "r8" && operands[1] == "r13") {
                if (position + 1 < block.instructions.size()) {
                    const auto next_index = block.instructions[position + 1];
                    const auto next_line =
                        trim(without_comment(lines[next_index]));
                    const auto next_mnemonic = mnemonic_of(next_line);
                    const auto next_operands = operands_of(next_line);
                    int value = 0;
                    if ((next_mnemonic == "sub" || next_mnemonic == "add") &&
                        next_operands.size() > 1 &&
                        next_operands[0] == "r8" &&
                        parse_immediate_operand(next_operands[1], value)) {
                        const int offset =
                            next_mnemonic == "sub" ? -value : value;
                        if (state.valid && state.offset == offset) {
                            if (dead != nullptr) {
                                (*dead)[line_index] = true;
                                (*dead)[next_index] = true;
                            }
                        } else {
                            state.valid = true;
                            state.offset = offset;
                        }
                        ++position;
                        continue;
                    }
                }
                state.valid = true;
                state.offset = 0;
                continue;
            }
            if (mnemonic == "call" || mnemonic == "enter" ||
                mnemonic == "leave") {
                state.valid = false;
                continue;
            }
            int destination = 0;
            if (!register_destination(line, destination) || destination != 8) {
                continue;
            }
            if ((mnemonic == "add" || mnemonic == "sub") &&
                operands.size() > 1) {
                int value = 0;
                if (state.valid && parse_immediate_operand(operands[1], value)) {
                    state.offset += mnemonic == "sub" ? -value : value;
                    continue;
                }
            }
            state.valid = false;
        }
        return state;
    };

    std::vector<std::optional<FrameAddressState>> entries(flow.blocks.size());
    std::vector<std::optional<FrameAddressState>> exits(flow.blocks.size());
    entries[0] = FrameAddressState{};
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t index = 0; index < flow.blocks.size(); ++index) {
            FrameAddressState entry;
            if (index != 0) {
                std::optional<FrameAddressState> joined;
                bool ready = true;
                for (const auto predecessor : flow.blocks[index].predecessors) {
                    if (!exits[predecessor]) {
                        ready = false;
                        break;
                    }
                    if (!joined) {
                        joined = *exits[predecessor];
                        continue;
                    }
                    if (!joined->valid || !exits[predecessor]->valid ||
                        joined->offset != exits[predecessor]->offset) {
                        joined = FrameAddressState{};
                    }
                }
                if (!ready) {
                    continue;
                }
                entry = joined.value_or(FrameAddressState{});
            }
            const auto exit = transfer(flow.blocks[index], entry, nullptr);
            if (!entries[index] || *entries[index] != entry || !exits[index] ||
                *exits[index] != exit) {
                entries[index] = entry;
                exits[index] = exit;
                changed = true;
            }
        }
    }

    std::vector<bool> dead(lines.size(), false);
    for (std::size_t index = 0; index < flow.blocks.size(); ++index) {
        static_cast<void>(transfer(flow.blocks[index],
                                   entries[index].value_or(FrameAddressState{}),
                                   &dead));
    }

    std::string result;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (dead[index]) {
            continue;
        }
        result += lines[index];
        result += '\n';
    }
    return result;
}

std::string fold_frame_addresses_to_lea(std::string_view assembly) {
    std::vector<std::string> lines;
    {
        std::string text(assembly);
        std::size_t start = 0;
        while (start <= text.size()) {
            const auto end = text.find('\n', start);
            lines.push_back(text.substr(start, end == std::string::npos
                                                   ? std::string::npos
                                                   : end - start));
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
    }

    auto flow = machine::build_control_flow(lines, analyze);
    std::vector<std::string> replacements(lines.size());
    const auto transfer = [&](const machine::TextBlock& block,
                              std::optional<int> delta,
                              std::vector<bool>* dead) {
        for (std::size_t position = 0; position < block.instructions.size();
             ++position) {
            const auto line_index = block.instructions[position];
            const auto line = trim(without_comment(lines[line_index]));
            const auto mnemonic = mnemonic_of(line);
            const auto operands = operands_of(line);
            if (mnemonic == "enter") {
                int frame = 0;
                int mask = 0;
                if (operands.size() > 1 &&
                    parse_immediate_operand(operands[0], frame) &&
                    parse_immediate_operand(operands[1], mask)) {
                    int saved = 0;
                    for (int bit = 0; bit < 16; ++bit) {
                        if ((mask & (1 << bit)) != 0) {
                            ++saved;
                        }
                    }
                    delta = -(frame + 4 * saved);
                } else {
                    delta.reset();
                }
                continue;
            }
            if (mnemonic == "leave") {
                delta.reset();
                continue;
            }
            if (mnemonic == "push") {
                if (delta) {
                    *delta -= 4;
                }
                continue;
            }
            if (mnemonic == "pop") {
                if (delta) {
                    *delta += 4;
                }
                int destination = 0;
                if (!operands.empty() &&
                    parse_register_operand(operands[0], destination) &&
                    destination == 15) {
                    delta.reset();
                }
                continue;
            }
            if (mnemonic == "call") {
                continue;
            }
            if ((mnemonic == "add" || mnemonic == "sub") &&
                operands.size() > 1 &&
                (operands[0] == "sp" || operands[0] == "r15")) {
                int value = 0;
                if (delta && parse_immediate_operand(operands[1], value)) {
                    *delta += mnemonic == "sub" ? -value : value;
                } else {
                    delta.reset();
                }
                continue;
            }
            if (mnemonic == "mov" && operands.size() > 1 &&
                operands[0] == "r8" && operands[1] == "r13" &&
                position + 1 < block.instructions.size()) {
                const auto next_index = block.instructions[position + 1];
                const auto next_line = trim(without_comment(lines[next_index]));
                const auto next_mnemonic = mnemonic_of(next_line);
                const auto next_operands = operands_of(next_line);
                int value = 0;
                if ((next_mnemonic == "sub" || next_mnemonic == "add") &&
                    next_operands.size() > 1 && next_operands[0] == "r8" &&
                    parse_immediate_operand(next_operands[1], value) && delta) {
                    const int offset = next_mnemonic == "sub" ? -value : value;
                    const int relative = offset - *delta;
                    std::string address = "[sp";
                    if (relative != 0) {
                        address += relative > 0 ? "+" : "";
                        address += std::to_string(relative);
                    }
                    address += "]";
                    replacements[line_index] = "  lea r8, " + address;
                    if (dead != nullptr) {
                        (*dead)[next_index] = true;
                    }
                    ++position;
                    continue;
                }
            }
            int destination = 0;
            if ((!operands.empty() && operands[0] == "sp") ||
                (register_destination(line, destination) && destination == 15)) {
                delta.reset();
            }
        }
        return delta;
    };

    std::vector<std::optional<int>> entries(flow.blocks.size());
    std::vector<std::optional<int>> exits(flow.blocks.size());
    std::vector<bool> initialized(flow.blocks.size(), false);
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t index = 0; index < flow.blocks.size(); ++index) {
            std::optional<int> entry;
            if (index == 0) {
                entry.reset();
            } else {
                std::optional<int> joined;
                bool ready = true;
                for (const auto predecessor : flow.blocks[index].predecessors) {
                    if (!exits[predecessor]) {
                        ready = false;
                        break;
                    }
                    if (!joined) {
                        joined = *exits[predecessor];
                        continue;
                    }
                    if (*joined != *exits[predecessor]) {
                        joined.reset();
                    }
                }
                if (!ready) {
                    continue;
                }
                entry = joined;
            }
            const auto exit = transfer(flow.blocks[index], entry, nullptr);
            if (!initialized[index] || entries[index] != entry ||
                exits[index] != exit) {
                entries[index] = entry;
                exits[index] = exit;
                initialized[index] = true;
                changed = true;
            }
        }
    }

    std::vector<bool> dead(lines.size(), false);
    for (std::size_t index = 0; index < flow.blocks.size(); ++index) {
        static_cast<void>(
            transfer(flow.blocks[index], entries[index], &dead));
    }

    std::string result;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (dead[index]) {
            continue;
        }
        if (!replacements[index].empty()) {
            result += replacements[index];
            result += '\n';
            continue;
        }
        result += lines[index];
        result += '\n';
    }
    return result;
}

}  // namespace krc::backend
