#include "backend/kr232_cfg.h"

#include <cctype>
#include <set>
#include <string>
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
            flow.targets.push_back(std::string(operands[0]));
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

bool is_removable_definition(std::string_view line, int& destination) {
    const auto text = trim(without_comment(line));
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

}  // namespace krc::backend
