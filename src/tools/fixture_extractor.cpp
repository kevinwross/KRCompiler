#include "krc/tools/fixture_extractor.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace krc::tools {

namespace {

bool is_identifier_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_identifier_char(char c) {
    return is_identifier_start(c) || (c >= '0' && c <= '9');
}

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

void append_utf8(std::string& out, std::uint32_t code_point) {
    if (code_point <= 0x7F) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

bool skip_space_and_comments(std::string_view source, std::size_t& position,
                             std::uint32_t& line) {
    bool skipped = false;
    for (;;) {
        while (position < source.size() && is_space(source[position])) {
            if (source[position] == '\n') {
                ++line;
            }
            ++position;
            skipped = true;
        }
        if (position + 1 >= source.size()) {
            return skipped;
        }
        if (source[position] == '/' && source[position + 1] == '/') {
            position += 2;
            while (position < source.size() && source[position] != '\n') {
                ++position;
            }
            skipped = true;
            continue;
        }
        if (source[position] == '/' && source[position + 1] == '*') {
            position += 2;
            while (position + 1 < source.size() &&
                   !(source[position] == '*' && source[position + 1] == '/')) {
                if (source[position] == '\n') {
                    ++line;
                }
                ++position;
            }
            if (position + 1 < source.size()) {
                position += 2;
            }
            skipped = true;
            continue;
        }
        return skipped;
    }
}

bool parse_hex_digits(std::string_view source, std::size_t& position, std::uint32_t& value,
                      std::size_t max_digits) {
    std::size_t count = 0;
    value = 0;
    while (position < source.size() && count < max_digits) {
        const char c = source[position];
        std::uint32_t digit = 0;
        if (c >= '0' && c <= '9') {
            digit = static_cast<std::uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<std::uint32_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            digit = static_cast<std::uint32_t>(c - 'A' + 10);
        } else {
            break;
        }
        value = (value << 4) | digit;
        ++position;
        ++count;
    }
    return count != 0;
}

bool parse_escape(std::string_view source, std::size_t& position, std::string& out,
                  std::uint32_t& line) {
    if (position >= source.size() || source[position] != '\\') {
        return false;
    }
    ++position;
    if (position >= source.size()) {
        return false;
    }
    const char c = source[position++];
    switch (c) {
    case 'n':
        out.push_back('\n');
        return true;
    case 't':
        out.push_back('\t');
        return true;
    case 'r':
        out.push_back('\r');
        return true;
    case 'a':
        out.push_back('\a');
        return true;
    case 'b':
        out.push_back('\b');
        return true;
    case 'f':
        out.push_back('\f');
        return true;
    case 'v':
        out.push_back('\v');
        return true;
    case '\\':
    case '\'':
    case '"':
    case '?':
        out.push_back(c);
        return true;
    case '\n':
        ++line;
        return true;
    case 'x': {
        std::uint32_t value = 0;
        if (!parse_hex_digits(source, position, value, 8)) {
            out.push_back('x');
            return true;
        }
        out.push_back(static_cast<char>(value));
        return true;
    }
    case 'u':
    case 'U': {
        std::uint32_t value = 0;
        const std::size_t digits = c == 'u' ? 4 : 8;
        std::size_t count = 0;
        while (position < source.size() && count < digits) {
            std::uint32_t digit = 0;
            const char h = source[position];
            if (h >= '0' && h <= '9') {
                digit = static_cast<std::uint32_t>(h - '0');
            } else if (h >= 'a' && h <= 'f') {
                digit = static_cast<std::uint32_t>(h - 'a' + 10);
            } else if (h >= 'A' && h <= 'F') {
                digit = static_cast<std::uint32_t>(h - 'A' + 10);
            } else {
                break;
            }
            value = (value << 4) | digit;
            ++position;
            ++count;
        }
        append_utf8(out, value);
        return true;
    }
    default:
        if (c >= '0' && c <= '7') {
            std::uint32_t value = static_cast<std::uint32_t>(c - '0');
            std::size_t count = 1;
            while (position < source.size() && count < 3 && source[position] >= '0' &&
                   source[position] <= '7') {
                value = (value << 3) | static_cast<std::uint32_t>(source[position] - '0');
                ++position;
                ++count;
            }
            out.push_back(static_cast<char>(value));
            return true;
        }
        out.push_back(c);
        return true;
    }
}

bool parse_raw_string(std::string_view source, std::size_t& position, std::string& out,
                      std::uint32_t& line) {
    // position points at the opening quote of R"delimiter(... )delimiter".
    if (position + 1 >= source.size() || source[position] != '"') {
        return false;
    }
    ++position;
    const std::size_t delimiter_start = position;
    while (position < source.size() && source[position] != '(') {
        if (source[position] == '\n') {
            return false;
        }
        ++position;
    }
    if (position >= source.size()) {
        return false;
    }
    const std::string_view delimiter = source.substr(delimiter_start, position - delimiter_start);
    ++position;
    const std::size_t content_start = position;
    while (position < source.size()) {
        if (source[position] == ')' && position + delimiter.size() + 1 < source.size()) {
            const auto tail = source.substr(position + 1, delimiter.size());
            if (tail == delimiter && source[position + 1 + delimiter.size()] == '"') {
                out.append(source.substr(content_start, position - content_start));
                for (std::size_t i = content_start; i < position; ++i) {
                    if (source[i] == '\n') {
                        ++line;
                    }
                }
                position += delimiter.size() + 2;
                return true;
            }
        }
        ++position;
    }
    return false;
}

bool parse_string_literal(std::string_view source, std::size_t& position, std::string& out,
                          std::uint32_t& line) {
    if (position >= source.size()) {
        return false;
    }

    if (source[position] == 'R' && position + 1 < source.size() && source[position + 1] == '"') {
        ++position;
        return parse_raw_string(source, position, out, line);
    }

    std::size_t prefix = 0;
    if (position + 2 < source.size() && source[position] == 'u' && source[position + 1] == '8' &&
        source[position + 2] == '"') {
        prefix = 2;
    } else if (position + 1 < source.size() &&
               (source[position] == 'u' || source[position] == 'U' || source[position] == 'L') &&
               source[position + 1] == '"') {
        prefix = 1;
    } else if (position + 2 < source.size() && source[position] == 'u' && source[position + 1] == '8' &&
               source[position + 2] == 'R') {
        position += 3;
        return parse_raw_string(source, position, out, line);
    } else if (position + 1 < source.size() &&
               (source[position] == 'u' || source[position] == 'U' || source[position] == 'L') &&
               source[position + 1] == 'R') {
        position += 2;
        return parse_raw_string(source, position, out, line);
    }

    position += prefix;
    if (position >= source.size() || source[position] != '"') {
        return false;
    }
    ++position;

    while (position < source.size()) {
        const char c = source[position];
        if (c == '"') {
            ++position;
            return true;
        }
        if (c == '\n') {
            return false;
        }
        if (c == '\\') {
            if (!parse_escape(source, position, out, line)) {
                return false;
            }
            continue;
        }
        out.push_back(c);
        ++position;
    }
    return false;
}

bool is_compile_call(std::string_view identifier) {
    return identifier == "compile" || identifier == "compile_and_run" ||
           identifier == "compile_source" || identifier == "compile_program" ||
           identifier == "compile_file";
}

}  // namespace

std::vector<ExtractedFixture> extract_fixtures(std::string_view source,
                                               std::string_view source_name) {
    std::vector<ExtractedFixture> fixtures;
    std::size_t position = 0;
    std::uint32_t line = 1;

    while (position < source.size()) {
        const char c = source[position];

        if (c == '\n') {
            ++line;
            ++position;
            continue;
        }
        if (c == '/' && position + 1 < source.size() &&
            (source[position + 1] == '/' || source[position + 1] == '*')) {
            skip_space_and_comments(source, position, line);
            continue;
        }
        if (c == '"' || c == 'R' ||
            ((c == 'u' || c == 'U' || c == 'L') && position + 1 < source.size())) {
            std::string ignored;
            const auto start = position;
            if (parse_string_literal(source, position, ignored, line)) {
                continue;
            }
            position = start + 1;
            continue;
        }
        if (!is_identifier_start(c)) {
            ++position;
            continue;
        }

        const auto identifier_start = position;
        while (position < source.size() && is_identifier_char(source[position])) {
            ++position;
        }
        const auto identifier = source.substr(identifier_start, position - identifier_start);
        auto probe = position;
        auto probe_line = line;
        skip_space_and_comments(source, probe, probe_line);
        if (!is_compile_call(identifier) || probe >= source.size() || source[probe] != '(') {
            continue;
        }

        const auto call_line = line;
        std::string text;
        bool found_string = false;
        std::size_t cursor = probe + 1;
        int depth = 0;
        bool closed = false;

        while (cursor < source.size()) {
            skip_space_and_comments(source, cursor, line);
            if (cursor >= source.size()) {
                break;
            }
            const char argument = source[cursor];
            if (argument == ')' && depth == 0) {
                ++cursor;
                closed = true;
                break;
            }
            if (argument == '(') {
                ++depth;
                ++cursor;
                continue;
            }
            if (argument == ')') {
                --depth;
                ++cursor;
                continue;
            }
            if (argument == '"' || argument == 'R' ||
                ((argument == 'u' || argument == 'U' || argument == 'L') &&
                 cursor + 1 < source.size())) {
                std::string part;
                const auto start = cursor;
                if (parse_string_literal(source, cursor, part, line)) {
                    text += part;
                    found_string = true;
                    continue;
                }
                cursor = start + 1;
                continue;
            }
            ++cursor;
        }

        if (closed && found_string) {
            fixtures.push_back(ExtractedFixture{std::string(source_name), call_line, std::move(text)});
        }
        position = cursor;
    }

    return fixtures;
}

}  // namespace krc::tools
