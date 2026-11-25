#include "backend/peephole.h"

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace krc::backend {

namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

bool is_identity(std::string_view line) {
    line = trim(line);
    if (line.rfind("MOV ", 0) == 0) {
        const auto comma = line.find(',');
        if (comma == std::string_view::npos) {
            return false;
        }
        auto destination = trim(line.substr(4, comma - 4));
        auto source = trim(line.substr(comma + 1));
        return destination == source;
    }
    for (const auto mnemonic : {"ADD ", "MUL "}) {
        if (line.rfind(mnemonic, 0) != 0) {
            continue;
        }
        const auto first = line.find(',');
        const auto second = first == std::string_view::npos ? std::string_view::npos
                                                            : line.find(',', first + 1);
        if (first == std::string_view::npos || second == std::string_view::npos) {
            return false;
        }
        const auto destination = trim(line.substr(std::string_view(mnemonic).size(),
                                                   first - std::string_view(mnemonic).size()));
        const auto source = trim(line.substr(first + 1, second - first - 1));
        auto immediate = trim(line.substr(second + 1));
        if (mnemonic == std::string_view("ADD ")) {
            return destination == source && (immediate == "#0" || immediate == "#0x0");
        }
        if (mnemonic == std::string_view("MUL ")) {
            return destination == source && immediate == "#1";
        }
    }
    return false;
}

// A label line: an identifier or `.L` name followed by a colon. A comment can
// mention a colon, so comments are excluded first.
bool is_label(std::string_view line) {
    line = trim(line);
    return !line.empty() && line.front() != ';' && line.back() == ':';
}

bool is_comment(std::string_view line) {
    line = trim(line);
    return !line.empty() && line.front() == ';';
}

// The target of an unconditional branch, or empty when the line is not one.
std::string_view branch_target(std::string_view line) {
    line = trim(line);
    if (line.empty() || line.front() == ';' || line.back() == ':') {
        return {};
    }
    const auto space = line.find_first_of(" \t");
    if (space == std::string_view::npos) {
        return {};
    }
    const auto mnemonic = line.substr(0, space);
    if (mnemonic != "JMP" && mnemonic != "BRA") {
        return {};
    }
    const auto target = trim(line.substr(space + 1));
    // A register target names no label and is always kept.
    if (target.empty() || target.front() == 'R' || target.front() == '[') {
        return {};
    }
    return target;
}

// The label text without its trailing colon.
std::string_view label_name(std::string_view line) {
    auto name = trim(line);
    name.remove_suffix(1);
    return trim(name);
}

}  // namespace

std::string peephole(std::string_view assembly) {
    std::istringstream input{std::string(assembly)};
    std::string line;
    std::string result;
    while (std::getline(input, line)) {
        if (is_identity(line)) {
            continue;
        }
        result += line;
        result += '\n';
    }
    return result;
}

std::string forward_store_loads(std::string_view assembly) {
    std::istringstream input{std::string(assembly)};
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    const auto parse = [](std::string_view text, std::string_view mnemonic,
                          std::string& reg, std::string& offset) {
        text = trim(text);
        if (text.rfind(mnemonic, 0) != 0) {
            return false;
        }
        text.remove_prefix(mnemonic.size());
        const auto comma = text.find(',');
        if (comma == std::string_view::npos) {
            return false;
        }
        reg = std::string(trim(text.substr(0, comma)));
        auto address = trim(text.substr(comma + 1));
        if (address.size() < 8 || address.front() != '[' ||
            address.back() != ']') {
            return false;
        }
        address.remove_prefix(1);
        address.remove_suffix(1);
        const auto plus = address.find('+');
        if (plus == std::string_view::npos) {
            return false;
        }
        if (trim(address.substr(0, plus)) != "R7") {
            return false;
        }
        offset = std::string(trim(address.substr(plus + 1)));
        return !reg.empty() && !offset.empty();
    };
    std::vector<bool> dead(lines.size(), false);
    std::vector<std::string> replacements(lines.size());
    for (std::size_t index = 0; index < lines.size(); ++index) {
        std::string reg;
        std::string offset;
        if (!parse(lines[index], "STORE ", reg, offset)) {
            continue;
        }
        std::size_t next = index + 1;
        while (next < lines.size()) {
            const auto text = trim(lines[next]);
            if (text.empty() || is_comment(text) ||
                (is_label(text) && label_name(text).rfind(".Ldbg.", 0) == 0)) {
                ++next;
                continue;
            }
            break;
        }
        if (next >= lines.size()) {
            continue;
        }
        std::string load_reg;
        std::string load_offset;
        if (parse(lines[next], "LOAD ", load_reg, load_offset) &&
            load_offset == offset) {
            if (load_reg == reg) {
                dead[next] = true;
            } else {
                replacements[next] = "        MOV " + load_reg + ", " + reg;
            }
        }
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

std::string coalesce_store_addresses(std::string_view assembly) {
    std::istringstream input{std::string(assembly)};
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    const auto register_name = [](std::string_view text, std::string& reg) {
        text = trim(text);
        if (text.size() < 2 || text.front() != 'R') {
            return false;
        }
        for (std::size_t index = 1; index < text.size(); ++index) {
            if (text[index] < '0' || text[index] > '9') {
                return false;
            }
        }
        reg = std::string(text);
        return true;
    };
    const auto mention_count = [](std::string_view text, std::string_view reg) {
        std::size_t count = 0;
        for (auto at = text.find(reg); at != std::string_view::npos;
             at = text.find(reg, at + reg.size())) {
            const bool before_ok =
                at == 0 || !(text[at - 1] >= '0' && text[at - 1] <= '9');
            const auto after = at + reg.size();
            const bool after_ok =
                after >= text.size() || !(text[after] >= '0' && text[after] <= '9');
            if (before_ok && after_ok) {
                ++count;
            }
        }
        return count;
    };
    std::vector<bool> dead(lines.size(), false);
    std::vector<std::string> replacements(lines.size());
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto move = trim(lines[index]);
        if (move.rfind("MOV ", 0) != 0) {
            continue;
        }
        const auto move_comma = move.find(',');
        if (move_comma == std::string_view::npos) {
            continue;
        }
        std::string destination;
        std::string source;
        if (!register_name(move.substr(4, move_comma - 4), destination) ||
            !register_name(move.substr(move_comma + 1), source)) {
            continue;
        }
        std::size_t store = index + 1;
        while (store < lines.size()) {
            const auto candidate = trim(lines[store]);
            if (candidate.empty() || is_comment(candidate) ||
                (is_label(candidate) &&
                 label_name(candidate).rfind(".Ldbg.", 0) == 0)) {
                ++store;
                continue;
            }
            break;
        }
        if (store >= lines.size()) {
            continue;
        }
        const auto text = trim(lines[store]);
        const bool is_store = text.rfind("STORE ", 0) == 0;
        const bool is_load = text.rfind("LOAD", 0) == 0;
        if (!is_store && !is_load) {
            continue;
        }
        const auto operation_space = text.find_first_of(" \t");
        const auto operation_mnemonic =
            operation_space == std::string_view::npos
                ? text
                : text.substr(0, operation_space);
        if (is_load && operation_mnemonic != "LOAD" &&
            operation_mnemonic != "LOAD8U" &&
            operation_mnemonic != "LOAD8S" &&
            operation_mnemonic != "LOAD16U" &&
            operation_mnemonic != "LOAD16S") {
            continue;
        }
        const auto comma = text.find(',');
        if (comma == std::string_view::npos) {
            continue;
        }
        std::string store_value;
        std::string load_destination;
        if (is_store) {
            store_value = std::string(trim(text.substr(6, comma - 6)));
        } else {
            load_destination = std::string(trim(text.substr(
                operation_mnemonic.size(),
                comma - operation_mnemonic.size())));
        }
        auto address = trim(text.substr(comma + 1));
        if (address.size() < 2 || address.front() != '[') {
            continue;
        }
        bool changed = false;
        if (is_store && store_value == destination) {
            store_value = source;
            changed = true;
        }
        std::string rewritten_address(address);
        for (auto at = rewritten_address.find(destination);
             at != std::string::npos;
             at = rewritten_address.find(destination, at + source.size())) {
            const bool before_ok = at == 0 || rewritten_address[at - 1] != 'R';
            const auto after = at + destination.size();
            const bool after_ok =
                after >= rewritten_address.size() ||
                !(rewritten_address[after] >= '0' && rewritten_address[after] <= '9');
            if (before_ok && after_ok) {
                rewritten_address.replace(at, destination.size(), source);
                changed = true;
            }
        }
        if (!changed) {
            continue;
        }
        bool safe = false;
        for (std::size_t next = store + 1; next < lines.size(); ++next) {
            const auto candidate = trim(lines[next]);
            if (candidate.empty() || is_comment(candidate) ||
                (is_label(candidate) &&
                 label_name(candidate).rfind(".Ldbg.", 0) == 0)) {
                continue;
            }
            if (is_label(candidate)) {
                break;
            }
            const auto space = candidate.find_first_of(" \t");
            const auto mnemonic =
                space == std::string_view::npos ? candidate
                                                : candidate.substr(0, space);
            if (mnemonic == "RET" || mnemonic == "HALT") {
                safe = destination != "R0";
                break;
            }
            if (mnemonic == "CALL" || mnemonic == "BRA" ||
                (mnemonic.size() > 1 && mnemonic.front() == 'J')) {
                break;
            }
            if (!mention_count(candidate, destination)) {
                continue;
            }
            if (mention_count(candidate, destination) != 1) {
                break;
            }
            if (mnemonic == "MOV" || mnemonic == "LOAD" ||
                mnemonic == "LOAD8U" || mnemonic == "LOAD8S" ||
                mnemonic == "LOAD16U" || mnemonic == "LOAD16S" ||
                mnemonic == "LEA" || mnemonic == "ADD" || mnemonic == "SUB" ||
                mnemonic == "MUL" || mnemonic == "AND" || mnemonic == "OR" ||
                mnemonic == "XOR" || mnemonic == "SHL" || mnemonic == "SHR" ||
                mnemonic == "SAR" || mnemonic == "NEG" || mnemonic == "NOT" ||
                mnemonic == "POP") {
                const auto first_comma = candidate.find(',');
                const auto first_operand = trim(candidate.substr(
                    mnemonic.size(), first_comma == std::string_view::npos
                                          ? std::string_view::npos
                                          : first_comma - mnemonic.size()));
                std::string written;
                safe = register_name(first_operand, written) &&
                       written == destination;
            }
            break;
        }
        if (safe) {
            dead[index] = true;
        }
        replacements[store] = is_store
                                  ? "        STORE " + store_value + ", " +
                                        rewritten_address
                                  : "        " + std::string(operation_mnemonic) +
                                        " " + load_destination + ", " +
                                        rewritten_address;
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

std::string combine_move_arithmetic(std::string_view assembly) {
    std::istringstream input{std::string(assembly)};
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    const auto register_name = [](std::string_view text, std::string& reg) {
        text = trim(text);
        if (text.size() < 2 || text.front() != 'R') {
            return false;
        }
        for (std::size_t index = 1; index < text.size(); ++index) {
            if (text[index] < '0' || text[index] > '9') {
                return false;
            }
        }
        reg = std::string(text);
        return true;
    };
    const auto is_combining = [](std::string_view mnemonic) {
        return mnemonic == "ADD" || mnemonic == "SUB" || mnemonic == "MUL" ||
               mnemonic == "AND" || mnemonic == "OR" || mnemonic == "XOR" ||
               mnemonic == "SHL" || mnemonic == "SHR" || mnemonic == "SAR";
    };
    std::vector<bool> dead(lines.size(), false);
    std::vector<std::string> replacements(lines.size());
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto move = trim(lines[index]);
        if (move.rfind("MOV ", 0) != 0) {
            continue;
        }
        const auto move_comma = move.find(',');
        if (move_comma == std::string_view::npos) {
            continue;
        }
        std::string destination;
        std::string source;
        if (!register_name(move.substr(4, move_comma - 4), destination) ||
            !register_name(move.substr(move_comma + 1), source)) {
            continue;
        }
        std::size_t next = index + 1;
        while (next < lines.size()) {
            const auto candidate = trim(lines[next]);
            if (candidate.empty() || is_comment(candidate) ||
                (is_label(candidate) &&
                 label_name(candidate).rfind(".Ldbg.", 0) == 0)) {
                ++next;
                continue;
            }
            break;
        }
        if (next >= lines.size()) {
            continue;
        }
        const auto operation = trim(lines[next]);
        const auto space = operation.find_first_of(" \t");
        if (space == std::string_view::npos) {
            continue;
        }
        const auto mnemonic = operation.substr(0, space);
        if (!is_combining(mnemonic)) {
            continue;
        }
        const auto first = operation.find(',', space);
        const auto second =
            first == std::string_view::npos ? std::string_view::npos
                                            : operation.find(',', first + 1);
        if (first == std::string_view::npos || second == std::string_view::npos) {
            continue;
        }
        std::string operation_destination;
        std::string operation_source;
        if (!register_name(operation.substr(space, first - space),
                           operation_destination) ||
            !register_name(operation.substr(first + 1, second - first - 1),
                           operation_source) ||
            operation_destination != destination ||
            operation_source != destination) {
            continue;
        }
        const auto third = trim(operation.substr(second + 1));
        if (third.empty()) {
            continue;
        }
        std::string third_operand(third);
        if (third_operand == destination) {
            third_operand = source;
        }
        replacements[next] = "        " + std::string(mnemonic) + " " +
                             destination + ", " + source + ", " + third_operand;
        dead[index] = true;
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

std::string coalesce_store_sources(std::string_view assembly) {
    std::istringstream input{std::string(assembly)};
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    const auto register_name = [](std::string_view text, std::string& reg) {
        text = trim(text);
        if (text.size() < 2 || text.front() != 'R') {
            return false;
        }
        for (std::size_t index = 1; index < text.size(); ++index) {
            if (text[index] < '0' || text[index] > '9') {
                return false;
            }
        }
        reg = std::string(text);
        return true;
    };
    std::vector<bool> dead(lines.size(), false);
    std::vector<std::string> replacements(lines.size());
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto text = trim(lines[index]);
        if (text.rfind("MOV ", 0) != 0) {
            continue;
        }
        const auto comma = text.find(',');
        if (comma == std::string_view::npos) {
            continue;
        }
        std::string destination;
        std::string source;
        if (!register_name(text.substr(4, comma - 4), destination) ||
            !register_name(text.substr(comma + 1), source) || destination != "R0") {
            continue;
        }
        std::size_t store = index + 1;
        while (store < lines.size()) {
            const auto candidate = trim(lines[store]);
            if (candidate.empty() || is_comment(candidate) ||
                (is_label(candidate) &&
                 label_name(candidate).rfind(".Ldbg.", 0) == 0)) {
                ++store;
                continue;
            }
            break;
        }
        if (store >= lines.size()) {
            continue;
        }
        const auto store_text = trim(lines[store]);
        if (store_text.rfind("STORE ", 0) != 0) {
            continue;
        }
        const auto store_comma = store_text.find(',');
        if (store_comma == std::string_view::npos) {
            continue;
        }
        std::string store_register;
        if (!register_name(store_text.substr(6, store_comma - 6), store_register) ||
            store_register != destination) {
            continue;
        }
        bool safe = false;
        for (std::size_t next = store + 1; next < lines.size(); ++next) {
            const auto candidate = trim(lines[next]);
            if (candidate.empty() || is_comment(candidate) ||
                (is_label(candidate) &&
                 label_name(candidate).rfind(".Ldbg.", 0) == 0)) {
                continue;
            }
            if (is_label(candidate)) {
                break;
            }
            const auto space = candidate.find_first_of(" \t");
            const auto mnemonic =
                space == std::string_view::npos ? candidate
                                                : candidate.substr(0, space);
            if (mnemonic == "RET" || mnemonic == "HALT") {
                safe = destination != "R0";
                break;
            }
            if (mnemonic == "CALL" || mnemonic == "BRA" ||
                (mnemonic.size() > 1 && mnemonic.front() == 'J') ||
                is_label(candidate)) {
                break;
            }
            if (candidate.find(destination) == std::string_view::npos) {
                continue;
            }
            std::size_t mentions = 0;
            for (auto at = candidate.find(destination); at != std::string_view::npos;
                 at = candidate.find(destination, at + destination.size())) {
                ++mentions;
            }
            if (mentions != 1) {
                // More than one mention means the instruction also reads R0;
                // the old value is still live.
                break;
            }
            // Only a write to R0 itself makes the move dead; a read, or a
            // write to some other register that merely mentions R0, does not.
            const bool writes_destination =
                mnemonic == "MOV" || mnemonic == "LOAD" ||
                mnemonic == "LOAD8U" || mnemonic == "LOAD8S" ||
                mnemonic == "LOAD16U" || mnemonic == "LOAD16S" ||
                mnemonic == "LEA" || mnemonic == "ADD" || mnemonic == "SUB" ||
                mnemonic == "MUL" || mnemonic == "DIVS" || mnemonic == "DIVU" ||
                mnemonic == "MODS" || mnemonic == "MODU" || mnemonic == "AND" ||
                mnemonic == "OR" || mnemonic == "XOR" || mnemonic == "SHL" ||
                mnemonic == "SHR" || mnemonic == "SAR" || mnemonic == "NEG" ||
                mnemonic == "NOT" || mnemonic == "POP";
            if (writes_destination) {
                const auto first_comma = candidate.find(',');
                const auto first_operand = trim(candidate.substr(
                    mnemonic.size(), first_comma == std::string_view::npos
                                          ? std::string_view::npos
                                          : first_comma - mnemonic.size()));
                safe = first_operand == destination;
            }
            break;
        }
        if (!safe) {
            continue;
        }
        std::string rewritten = lines[store];
        const auto position = rewritten.find("STORE " + destination);
        if (position == std::string::npos) {
            continue;
        }
        rewritten.replace(position, ("STORE " + destination).size(),
                          "STORE " + source);
        replacements[store] = rewritten;
        dead[index] = true;
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

std::string propagate_copies(std::string_view assembly) {
    std::istringstream input{std::string(assembly)};
    std::string line;
    std::string result;
    int value_ids[16];
    int next_value = 1;
    const auto reset_all = [&]() {
        for (int& id : value_ids) {
            id = next_value++;
        }
    };
    reset_all();
    const auto parse_register = [](std::string_view text) -> int {
        text = trim(text);
        if (text.size() < 2 || text.front() != 'R') {
            return -1;
        }
        int number = 0;
        for (std::size_t index = 1; index < text.size(); ++index) {
            if (text[index] < '0' || text[index] > '9') {
                return -1;
            }
            number = number * 10 + (text[index] - '0');
        }
        return number < 16 ? number : -1;
    };
    const auto is_branch = [](std::string_view mnemonic) {
        return mnemonic == "JMP" || mnemonic == "BRA" || mnemonic == "JZ" ||
               mnemonic == "JNZ" || mnemonic == "JCS" || mnemonic == "JNC" ||
               mnemonic == "JGT" || mnemonic == "JGE" || mnemonic == "JLT" ||
               mnemonic == "JLE";
    };
    const auto writes_destination = [](std::string_view mnemonic) {
        return mnemonic == "LOAD" || mnemonic == "LOAD8U" ||
               mnemonic == "LOAD8S" || mnemonic == "LOAD16U" ||
               mnemonic == "LOAD16S" || mnemonic == "LEA" ||
               mnemonic == "ADD" || mnemonic == "SUB" ||
               mnemonic == "MUL" || mnemonic == "DIVS" ||
               mnemonic == "DIVU" || mnemonic == "MODS" ||
               mnemonic == "MODU" || mnemonic == "AND" || mnemonic == "OR" ||
               mnemonic == "XOR" || mnemonic == "SHL" || mnemonic == "SHR" ||
               mnemonic == "SAR" || mnemonic == "NEG" || mnemonic == "NOT" ||
               mnemonic == "POP";
    };
    while (std::getline(input, line)) {
        const auto text = trim(line);
        if (text.empty() || is_comment(text)) {
            result += line;
            result += '\n';
            continue;
        }
        if (is_label(text)) {
            const auto name = label_name(text);
            // A `.Ldbg` label only names a source line; control flows straight
            // through it. Every other label is a possible branch target.
            if (name.rfind(".Ldbg.", 0) != 0) {
                reset_all();
            }
            result += line;
            result += '\n';
            continue;
        }
        const auto space = text.find_first_of(" \t");
        const auto mnemonic = space == std::string_view::npos
                                  ? text
                                  : text.substr(0, space);
        if (mnemonic == "MOV") {
            const auto comma = text.find(',');
            if (comma != std::string_view::npos) {
                const auto destination = parse_register(text.substr(4, comma - 4));
                const auto source = parse_register(text.substr(comma + 1));
                if (destination >= 0) {
                    if (source >= 0) {
                        if (value_ids[destination] == value_ids[source]) {
                            continue;
                        }
                        value_ids[destination] = value_ids[source];
                    } else {
                        value_ids[destination] = next_value++;
                    }
                }
            }
            result += line;
            result += '\n';
            continue;
        }
        if (mnemonic == "CALL") {
            // Caller-saved: R0-R6 and R12-R15. R7 is FP, R8-R11 survive, and
            // R11 is the thread pointer.
            for (int reg = 0; reg <= 6; ++reg) {
                value_ids[reg] = next_value++;
            }
            for (int reg = 12; reg <= 15; ++reg) {
                value_ids[reg] = next_value++;
            }
            result += line;
            result += '\n';
            continue;
        }
        if (is_branch(mnemonic) || mnemonic == "RET" || mnemonic == "HALT" ||
            mnemonic == "SYSCALL") {
            reset_all();
            result += line;
            result += '\n';
            continue;
        }
        if (writes_destination(mnemonic)) {
            const auto comma = text.find(',');
            const auto destination =
                parse_register(text.substr(mnemonic.size(),
                                           comma == std::string_view::npos
                                               ? std::string_view::npos
                                               : comma - mnemonic.size()));
            if (destination >= 0) {
                value_ids[destination] = next_value++;
            }
            result += line;
            result += '\n';
            continue;
        }
        if (mnemonic == "STORE" || mnemonic == "CMP" || mnemonic == "PUSH") {
            // Read-only for integer registers.
            result += line;
            result += '\n';
            continue;
        }
        if (mnemonic.size() > 1 && mnemonic.front() == 'F') {
            // Most floating instructions name an F register, but a conversion
            // such as `FCVT.W.D R0, F0` writes an integer destination.
            const auto comma = text.find(',');
            const auto destination =
                parse_register(text.substr(mnemonic.size(),
                                           comma == std::string_view::npos
                                               ? std::string_view::npos
                                               : comma - mnemonic.size()));
            if (destination >= 0) {
                value_ids[destination] = next_value++;
            }
            result += line;
            result += '\n';
            continue;
        }
        // A directive or an unknown form may write anything.
        reset_all();
        result += line;
        result += '\n';
    }
    return result;
}

std::string remove_redundant_jumps(std::string_view assembly) {
    std::istringstream input{std::string(assembly)};
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    std::vector<bool> dead(lines.size(), false);
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto target = branch_target(lines[index]);
        if (target.empty()) {
            continue;
        }
        // Look past labels and comments: control falling through them still
        // reaches the branch's target, so the branch itself is dead.
        for (std::size_t next = index + 1; next < lines.size(); ++next) {
            if (is_comment(lines[next])) {
                continue;
            }
            if (!is_label(lines[next])) {
                break;
            }
            if (label_name(lines[next]) == target) {
                dead[index] = true;
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
