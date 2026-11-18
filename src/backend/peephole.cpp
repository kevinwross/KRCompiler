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
