#include "backend/peephole.h"

#include <sstream>
#include <string>
#include <string_view>

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

}  // namespace krc::backend
