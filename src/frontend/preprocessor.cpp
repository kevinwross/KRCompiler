#include "krc/frontend/preprocessor.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace krc::frontend {

namespace {

bool is_identifier_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

std::string quote_string(std::string_view text) {
    std::string result = "\"";
    for (const char c : text) {
        if (c == '\\' || c == '"') {
            result.push_back('\\');
        }
        result.push_back(c);
    }
    result.push_back('"');
    return result;
}

std::optional<long long> parse_integer(std::string_view text) {
    while (!text.empty()) {
        const char last = text.back();
        if (last == 'u' || last == 'U' || last == 'l' || last == 'L') {
            text.remove_suffix(1);
        } else {
            break;
        }
    }
    if (text.empty()) {
        return std::nullopt;
    }

    int base = 10;
    std::size_t index = 0;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        index = 2;
    } else if (text.size() > 1 && text[0] == '0') {
        base = 8;
        index = 1;
    }

    unsigned long long value = 0;
    bool saw_digit = false;
    for (; index < text.size(); ++index) {
        const char c = text[index];
        unsigned digit = 0;
        if (c >= '0' && c <= '9') {
            digit = static_cast<unsigned>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<unsigned>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            digit = static_cast<unsigned>(c - 'A' + 10);
        } else {
            return std::nullopt;
        }
        if (digit >= static_cast<unsigned>(base)) {
            return std::nullopt;
        }
        value = value * static_cast<unsigned>(base) + digit;
        saw_digit = true;
    }
    if (!saw_digit) {
        return std::nullopt;
    }
    return static_cast<long long>(value);
}

std::optional<long long> parse_character(std::string_view text) {
    if (text.size() < 3 || text.front() != '\'') {
        return std::nullopt;
    }
    std::size_t index = 1;
    long long value = 0;
    if (text[index] == '\\') {
        ++index;
        if (index >= text.size()) {
            return std::nullopt;
        }
        const char escape = text[index++];
        switch (escape) {
        case 'n':
            value = '\n';
            break;
        case 't':
            value = '\t';
            break;
        case 'r':
            value = '\r';
            break;
        case '\\':
            value = '\\';
            break;
        case '\'':
            value = '\'';
            break;
        case '"':
            value = '"';
            break;
        case '0':
            value = 0;
            break;
        default:
            if (escape >= '0' && escape <= '7') {
                value = escape - '0';
                for (int i = 0; i < 2 && index < text.size() && text[index] >= '0' &&
                                text[index] <= '7';
                     ++i) {
                    value = value * 8 + (text[index++] - '0');
                }
            } else {
                return std::nullopt;
            }
            break;
        }
    } else {
        value = static_cast<unsigned char>(text[index++]);
    }
    if (index >= text.size() || text[index] != '\'') {
        return std::nullopt;
    }
    return value;
}

TokenKind classify_pasted(std::string_view text) {
    if (text.empty()) {
        return TokenKind::Punctuator;
    }
    if (is_identifier_start(text[0])) {
        return TokenKind::Identifier;
    }
    if (text[0] >= '0' && text[0] <= '9') {
        return TokenKind::Integer;
    }
    return TokenKind::Punctuator;
}

class IfParser {
public:
    IfParser(const std::vector<Token>& tokens, DiagnosticEngine& diagnostics,
             SourceLocation location)
        : tokens_(tokens), diagnostics_(diagnostics), location_(location) {}

    long long parse() {
        const auto value = parse_conditional();
        if (!at_end()) {
            diagnostics_.report(Severity::Error, location_,
                                "unexpected token in preprocessor expression");
        }
        return value;
    }

private:
    [[nodiscard]] bool at_end() const { return position_ >= tokens_.size(); }

    [[nodiscard]] const Token& peek() const {
        static const Token eof{TokenKind::Eof, {}, SourceLocation{}};
        return at_end() ? eof : tokens_[position_];
    }

    bool match(std::string_view spelling) {
        if (!at_end() && tokens_[position_].text == spelling) {
            ++position_;
            return true;
        }
        return false;
    }

    long long parse_conditional() {
        auto condition = parse_logical_or();
        if (match("?")) {
            const auto when_true = parse_conditional();
            if (!match(":")) {
                diagnostics_.report(Severity::Error, location_, "expected ':' in '?:'");
                return 0;
            }
            const auto when_false = parse_conditional();
            return condition != 0 ? when_true : when_false;
        }
        return condition;
    }

    long long parse_logical_or() {
        auto value = parse_logical_and();
        while (match("||")) {
            const auto right = parse_logical_and();
            value = (value != 0 || right != 0) ? 1 : 0;
        }
        return value;
    }

    long long parse_logical_and() {
        auto value = parse_bit_or();
        while (match("&&")) {
            const auto right = parse_bit_or();
            value = (value != 0 && right != 0) ? 1 : 0;
        }
        return value;
    }

    long long parse_bit_or() {
        auto value = parse_bit_xor();
        while (match("|")) {
            value |= parse_bit_xor();
        }
        return value;
    }

    long long parse_bit_xor() {
        auto value = parse_bit_and();
        while (match("^")) {
            value ^= parse_bit_and();
        }
        return value;
    }

    long long parse_bit_and() {
        auto value = parse_equality();
        while (match("&")) {
            value &= parse_equality();
        }
        return value;
    }

    long long parse_equality() {
        auto value = parse_relational();
        for (;;) {
            if (match("==")) {
                value = value == parse_relational() ? 1 : 0;
            } else if (match("!=")) {
                value = value != parse_relational() ? 1 : 0;
            } else {
                return value;
            }
        }
    }

    long long parse_relational() {
        auto value = parse_shift();
        for (;;) {
            if (match("<")) {
                value = value < parse_shift() ? 1 : 0;
            } else if (match("<=")) {
                value = value <= parse_shift() ? 1 : 0;
            } else if (match(">")) {
                value = value > parse_shift() ? 1 : 0;
            } else if (match(">=")) {
                value = value >= parse_shift() ? 1 : 0;
            } else {
                return value;
            }
        }
    }

    long long parse_shift() {
        auto value = parse_additive();
        for (;;) {
            if (match("<<")) {
                value = value << parse_additive();
            } else if (match(">>")) {
                value = value >> parse_additive();
            } else {
                return value;
            }
        }
    }

    long long parse_additive() {
        auto value = parse_multiplicative();
        for (;;) {
            if (match("+")) {
                value += parse_multiplicative();
            } else if (match("-")) {
                value -= parse_multiplicative();
            } else {
                return value;
            }
        }
    }

    long long parse_multiplicative() {
        auto value = parse_unary();
        for (;;) {
            if (match("*")) {
                value *= parse_unary();
            } else if (match("/")) {
                const auto divisor = parse_unary();
                if (divisor == 0) {
                    diagnostics_.report(Severity::Error, location_,
                                        "division by zero in preprocessor expression");
                    value = 0;
                } else {
                    value /= divisor;
                }
            } else if (match("%")) {
                const auto divisor = parse_unary();
                if (divisor == 0) {
                    diagnostics_.report(Severity::Error, location_,
                                        "division by zero in preprocessor expression");
                    value = 0;
                } else {
                    value %= divisor;
                }
            } else {
                return value;
            }
        }
    }

    long long parse_unary() {
        if (match("+")) {
            return parse_unary();
        }
        if (match("-")) {
            return -parse_unary();
        }
        if (match("!")) {
            return parse_unary() == 0 ? 1 : 0;
        }
        if (match("~")) {
            return ~parse_unary();
        }
        return parse_primary();
    }

    long long parse_primary() {
        if (at_end()) {
            diagnostics_.report(Severity::Error, location_,
                                "expected expression after preprocessor directive");
            return 0;
        }
        if (match("(")) {
            const auto value = parse_conditional();
            if (!match(")")) {
                diagnostics_.report(Severity::Error, location_,
                                    "expected ')' in preprocessor expression");
            }
            return value;
        }

        const Token token = tokens_[position_++];
        if (token.kind == TokenKind::Integer) {
            const auto value = parse_integer(token.text);
            if (!value) {
                diagnostics_.report(Severity::Error, token.location,
                                    "invalid integer in preprocessor expression");
                return 0;
            }
            return *value;
        }
        if (token.kind == TokenKind::Char) {
            const auto value = parse_character(token.text);
            if (!value) {
                diagnostics_.report(Severity::Error, token.location,
                                    "invalid character constant in preprocessor expression");
                return 0;
            }
            return *value;
        }
        if (token.kind == TokenKind::Identifier) {
            return 0;
        }

        diagnostics_.report(Severity::Error, token.location,
                            "unexpected token in preprocessor expression");
        return 0;
    }

    const std::vector<Token>& tokens_;
    DiagnosticEngine& diagnostics_;
    SourceLocation location_;
    std::size_t position_ = 0;
};

}  // namespace

Preprocessor::Preprocessor(SourceManager& sources, DiagnosticEngine& diagnostics,
                           const target::TargetProfile& profile, Options options)
    : sources_(sources), diagnostics_(diagnostics), profile_(profile),
      options_(std::move(options)) {
    seed_predefined();
}

void Preprocessor::seed_predefined() {
    define_object("__STDC__", "1");
    define_object("__STDC_VERSION__", "201112L");
    define_object("__KRC__", "1");
    define_object("__DATE__", "\"Jan  1 1970\"");
    define_object("__TIME__", "\"00:00:00\"");
    define_object(std::string(profile_.macro), "1");
    if (profile_.char_unsigned) {
        define_object("__CHAR_UNSIGNED__", "1");
    }
}

void Preprocessor::define_object(std::string name, std::string replacement) {
    Macro macro;
    macro.name = name;
    const auto kind = replacement.size() >= 2 && replacement.front() == '"'
                          ? TokenKind::String
                          : TokenKind::Integer;
    macro.replacement.push_back(Token{kind, std::move(replacement), SourceLocation{}});
    macros_[name] = std::move(macro);
}

bool Preprocessor::is_defined(std::string_view name) const {
    return macros_.find(std::string(name)) != macros_.end();
}

bool Preprocessor::is_active() const {
    return conditionals_.empty() || conditionals_.back().active;
}

int Preprocessor::line_of(const Token& token) const {
    if (!token.location.valid()) {
        return 0;
    }
    return static_cast<int>(sources_.physical_line(token.location));
}

Token Preprocessor::make_token(TokenKind kind, std::string text,
                               SourceLocation location) const {
    return Token{kind, std::move(text), location};
}

std::optional<std::vector<Token>> Preprocessor::preprocess_file(
    const std::filesystem::path& path) {
    std::string error;
    const auto file_id = sources_.load_file(path, error);
    if (!file_id) {
        diagnostics_.report(Severity::Error, std::move(error));
        return std::nullopt;
    }
    return preprocess(*file_id);
}

std::vector<Token> Preprocessor::preprocess(std::uint32_t file_id) {
    const auto file_name =
        std::filesystem::path(sources_.file(file_id).name()).lexically_normal().string();
    if (once_files_.find(file_name) != once_files_.end()) {
        return {};
    }

    Lexer lexer(sources_, diagnostics_, file_id);
    auto tokens = lexer.tokenize();

    std::vector<Token> output;
    std::vector<Token> segment;
    int last_line = 0;
    const auto initial_conditionals = conditionals_.size();

    const auto flush_segment = [&]() {
        if (segment.empty()) {
            return;
        }
        auto expanded = expand(segment, {}, 0);
        output.insert(output.end(), std::make_move_iterator(expanded.begin()),
                      std::make_move_iterator(expanded.end()));
        segment.clear();
    };

    for (std::size_t index = 0; index < tokens.size();) {
        if (tokens[index].is_eof()) {
            break;
        }
        const auto line = line_of(tokens[index]);
        if (tokens[index].is("#") && (index == 0 || line > last_line)) {
            if (is_active()) {
                flush_segment();
            }
            std::vector<Token> directive;
            std::size_t next = index + 1;
            while (next < tokens.size() && !tokens[next].is_eof() &&
                   line_of(tokens[next]) == line) {
                directive.push_back(tokens[next++]);
            }
            handle_directive(directive, file_id, output);
            last_line = line;
            index = next;
            continue;
        }

        if (is_active()) {
            segment.push_back(tokens[index]);
        }
        last_line = line;
        ++index;
    }

    if (is_active()) {
        flush_segment();
    }
    if (conditionals_.size() != initial_conditionals) {
        diagnostics_.report(Severity::Error, SourceLocation{},
                            "unterminated conditional directive");
        conditionals_.resize(initial_conditionals);
    }
    return output;
}

void Preprocessor::handle_directive(const std::vector<Token>& directive,
                                    std::uint32_t file_id, std::vector<Token>& output) {
    if (directive.empty()) {
        diagnostics_.report(Severity::Error, SourceLocation{}, "empty preprocessor directive");
        return;
    }

    const std::string& name = directive[0].text;
    if (name == "if") {
        handle_if_expression(directive);
    } else if (name == "ifdef") {
        handle_if(directive, false);
    } else if (name == "ifndef") {
        handle_if(directive, true);
    } else if (name == "elif") {
        handle_elif(directive);
    } else if (name == "else") {
        handle_else();
    } else if (name == "endif") {
        handle_endif();
    } else if (name == "define") {
        if (is_active()) {
            handle_define(directive);
        }
    } else if (name == "undef") {
        if (is_active()) {
            handle_undef(directive);
        }
    } else if (name == "include") {
        if (is_active()) {
            handle_include(directive, file_id, output);
        }
    } else if (name == "error") {
        if (is_active()) {
            handle_error(directive, Severity::Error);
        }
    } else if (name == "warning") {
        if (is_active()) {
            handle_error(directive, Severity::Warning);
        }
    } else if (name == "pragma") {
        if (is_active() && directive.size() > 1 && directive[1].text == "once") {
            once_files_.insert(
                std::filesystem::path(sources_.file(file_id).name()).lexically_normal().string());
        }
    } else if (name == "line") {
        if (is_active()) {
            handle_line_directive(directive, file_id);
        }
    } else if (!name.empty() && name[0] >= '0' && name[0] <= '9') {
        if (is_active()) {
            handle_line_directive(directive, file_id);
        }
    } else if (is_active()) {
        diagnostics_.report(Severity::Warning, directive[0].location,
                            "unknown preprocessor directive '#" + name + "'");
    }
}

void Preprocessor::handle_define(const std::vector<Token>& directive) {
    if (directive.size() < 2 || directive[1].kind != TokenKind::Identifier) {
        diagnostics_.report(Severity::Error,
                            directive.empty() ? SourceLocation{} : directive[0].location,
                            "expected macro name after #define");
        return;
    }

    Macro macro;
    macro.name = directive[1].text;
    macro.location = directive[1].location;
    std::size_t index = 2;

    if (index + 1 < directive.size() && directive[index].is("(") &&
        directive[index].location.offset ==
            directive[1].location.offset + directive[1].text.size()) {
        macro.function_like = true;
        ++index;
        if (index < directive.size() && directive[index].is(")")) {
            ++index;
        } else {
            for (;;) {
                if (index >= directive.size()) {
                    diagnostics_.report(Severity::Error, macro.location,
                                        "unterminated macro parameter list");
                    return;
                }
                if (directive[index].is("...")) {
                    macro.variadic = true;
                    ++index;
                    if (index >= directive.size() || !directive[index].is(")")) {
                        diagnostics_.report(Severity::Error, macro.location,
                                            "expected ')' after '...'");
                        return;
                    }
                    ++index;
                    break;
                }
                if (directive[index].kind != TokenKind::Identifier) {
                    diagnostics_.report(Severity::Error, directive[index].location,
                                        "expected macro parameter name");
                    return;
                }
                macro.parameters.push_back(directive[index].text);
                ++index;
                if (index < directive.size() && directive[index].is(",")) {
                    ++index;
                    continue;
                }
                if (index < directive.size() && directive[index].is(")")) {
                    ++index;
                    break;
                }
                diagnostics_.report(Severity::Error, macro.location,
                                    "expected ',' or ')' in macro parameter list");
                return;
            }
        }
    }

    macro.replacement.assign(directive.begin() + static_cast<std::ptrdiff_t>(index),
                             directive.end());
    macros_[macro.name] = std::move(macro);
}

void Preprocessor::handle_undef(const std::vector<Token>& directive) {
    if (directive.size() < 2 || directive[1].kind != TokenKind::Identifier) {
        diagnostics_.report(Severity::Error, SourceLocation{},
                            "expected macro name after #undef");
        return;
    }
    macros_.erase(directive[1].text);
}

void Preprocessor::handle_include(const std::vector<Token>& directive,
                                  std::uint32_t file_id, std::vector<Token>& output) {
    if (directive.size() < 2) {
        diagnostics_.report(Severity::Error, directive[0].location,
                            "expected header name after #include");
        return;
    }

    std::string header;
    bool quoted = false;
    if (directive[1].kind == TokenKind::String) {
        header = decode_string_literal(directive[1]);
        quoted = true;
    } else if (directive[1].is("<")) {
        for (std::size_t index = 2; index < directive.size(); ++index) {
            if (directive[index].is(">")) {
                break;
            }
            header += directive[index].text;
        }
        if (header.empty() || directive.back().text != ">") {
            diagnostics_.report(Severity::Error, directive[0].location,
                                "malformed #include header name");
            return;
        }
    } else {
        diagnostics_.report(Severity::Error, directive[0].location,
                            "expected \"file\" or <file> after #include");
        return;
    }

    const auto path = resolve_include(header, quoted, file_id);
    if (!path) {
        diagnostics_.report(Severity::Error, directive[0].location,
                            "header not found: '" + header + "'");
        return;
    }

    std::string error;
    const auto included_file = sources_.load_file(*path, error);
    if (!included_file) {
        diagnostics_.report(Severity::Error, directive[0].location, std::move(error));
        return;
    }

    sources_.push_include(*included_file);
    auto included = preprocess(*included_file);
    sources_.pop_include();
    output.insert(output.end(), std::make_move_iterator(included.begin()),
                  std::make_move_iterator(included.end()));
}

std::optional<std::filesystem::path> Preprocessor::resolve_include(
    const std::string& name, bool quoted, std::uint32_t including_file) const {
    std::vector<std::filesystem::path> candidates;
    const std::filesystem::path header(name);
    if (quoted) {
        const std::filesystem::path including(sources_.file(including_file).name());
        if (including.has_parent_path()) {
            candidates.push_back(including.parent_path() / header);
        }
    }
    for (const auto& directory : options_.include_paths) {
        candidates.push_back(directory / header);
    }

    std::error_code error;
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate, error) && !error) {
            return candidate;
        }
        error.clear();
    }
    return std::nullopt;
}

std::string Preprocessor::decode_string_literal(const Token& token) const {
    std::string_view text = token.text;
    if (text.size() >= 2 && (text[0] == 'u' || text[0] == 'U' || text[0] == 'L')) {
        if (text.size() >= 3 && text[0] == 'u' && text[1] == '8') {
            text.remove_prefix(2);
        } else {
            text.remove_prefix(1);
        }
    }
    if (text.size() < 2 || text.front() != '"') {
        return {};
    }
    text.remove_prefix(1);
    if (!text.empty() && text.back() == '"') {
        text.remove_suffix(1);
    }

    std::string result;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '\\' || index + 1 >= text.size()) {
            result.push_back(text[index]);
            continue;
        }
        const char escape = text[++index];
        switch (escape) {
        case 'n':
            result.push_back('\n');
            break;
        case 't':
            result.push_back('\t');
            break;
        case 'r':
            result.push_back('\r');
            break;
        case '\\':
            result.push_back('\\');
            break;
        case '"':
            result.push_back('"');
            break;
        default:
            result.push_back(escape);
            break;
        }
    }
    return result;
}

void Preprocessor::handle_if_expression(const std::vector<Token>& directive) {
    const bool parent_active = is_active();
    if (!parent_active) {
        conditionals_.push_back(Conditional{false, false, false, false});
        return;
    }
    const auto value = evaluate_if(directive, directive[0]);
    const bool active = value != 0;
    conditionals_.push_back(Conditional{true, active, active, false});
}

void Preprocessor::handle_if(const std::vector<Token>& directive, bool negate) {
    const bool parent_active = is_active();
    if (!parent_active) {
        conditionals_.push_back(Conditional{false, false, false, false});
        return;
    }
    if (directive.size() < 2) {
        diagnostics_.report(Severity::Error, directive[0].location,
                            "expected macro name after conditional directive");
        conditionals_.push_back(Conditional{true, false, false, false});
        return;
    }
    const bool defined = is_defined(directive[1].text);
    const bool active = negate ? !defined : defined;
    conditionals_.push_back(Conditional{true, active, active, false});
}

void Preprocessor::handle_elif(const std::vector<Token>& directive) {
    if (conditionals_.empty()) {
        diagnostics_.report(Severity::Error, directive[0].location, "#elif without #if");
        return;
    }
    auto& conditional = conditionals_.back();
    if (conditional.else_seen) {
        diagnostics_.report(Severity::Error, directive[0].location, "#elif after #else");
        return;
    }
    if (!conditional.parent_active || conditional.taken) {
        conditional.active = false;
        return;
    }
    const auto value = evaluate_if(directive, directive[0]);
    conditional.active = value != 0;
    conditional.taken = conditional.active;
}

void Preprocessor::handle_else() {
    if (conditionals_.empty()) {
        diagnostics_.report(Severity::Error, SourceLocation{}, "#else without #if");
        return;
    }
    auto& conditional = conditionals_.back();
    if (conditional.else_seen) {
        diagnostics_.report(Severity::Error, SourceLocation{}, "duplicate #else");
        return;
    }
    conditional.else_seen = true;
    conditional.active = conditional.parent_active && !conditional.taken;
    conditional.taken = conditional.taken || conditional.active;
}

void Preprocessor::handle_endif() {
    if (conditionals_.empty()) {
        diagnostics_.report(Severity::Error, SourceLocation{}, "#endif without #if");
        return;
    }
    conditionals_.pop_back();
}

void Preprocessor::handle_line_directive(const std::vector<Token>& directive,
                                         std::uint32_t file_id) {
    if (directive.empty()) {
        return;
    }
    const std::size_t value_index = directive[0].text == "line" ? 1 : 0;
    if (directive.size() <= value_index) {
        diagnostics_.report(Severity::Error, directive[0].location,
                            "#line requires a line number");
        return;
    }
    const auto value = parse_integer(directive[value_index].text);
    if (!value || *value <= 0) {
        diagnostics_.report(Severity::Error, directive[value_index].location,
                            "#line requires a positive integer line number");
        return;
    }
    const auto physical = sources_.physical_line(directive[0].location);
    std::string file_name;
    if (directive.size() > value_index + 1 &&
        directive[value_index + 1].kind == TokenKind::String) {
        file_name = decode_string_literal(directive[value_index + 1]);
    }
    sources_.add_line_directive(file_id, directive[0].location.offset, physical + 1,
                                static_cast<std::uint32_t>(*value), std::move(file_name));
}

void Preprocessor::handle_error(const std::vector<Token>& directive, Severity severity) {
    std::string message;
    for (std::size_t index = 1; index < directive.size(); ++index) {
        if (!message.empty()) {
            message.push_back(' ');
        }
        message += directive[index].text;
    }
    diagnostics_.report(severity,
                        directive.empty() ? SourceLocation{} : directive[0].location,
                        std::move(message));
}

long long Preprocessor::evaluate_if(const std::vector<Token>& tokens,
                                    const Token& directive_token) {
    std::vector<Token> replaced;
    for (std::size_t index = 1; index < tokens.size(); ++index) {
        if (tokens[index].kind == TokenKind::Identifier && tokens[index].text == "defined") {
            ++index;
            std::string name;
            if (index < tokens.size() && tokens[index].is("(")) {
                ++index;
                if (index >= tokens.size() || tokens[index].kind != TokenKind::Identifier) {
                    diagnostics_.report(Severity::Error, directive_token.location,
                                        "expected identifier after 'defined('");
                    return 0;
                }
                name = tokens[index].text;
                ++index;
                if (index >= tokens.size() || !tokens[index].is(")")) {
                    diagnostics_.report(Severity::Error, directive_token.location,
                                        "expected ')' after 'defined('");
                    return 0;
                }
            } else if (index < tokens.size() &&
                       tokens[index].kind == TokenKind::Identifier) {
                name = tokens[index].text;
            } else {
                diagnostics_.report(Severity::Error, directive_token.location,
                                    "expected identifier after 'defined'");
                return 0;
            }
            replaced.push_back(make_token(TokenKind::Integer,
                                          is_defined(name) ? "1" : "0",
                                          directive_token.location));
        } else {
            replaced.push_back(tokens[index]);
        }
    }

    auto expanded = expand(replaced, {}, 0);

    std::vector<Token> with_has_include;
    for (std::size_t index = 0; index < expanded.size(); ++index) {
        if (expanded[index].kind == TokenKind::Identifier &&
            expanded[index].text == "__has_include" && index + 1 < expanded.size() &&
            expanded[index + 1].is("(")) {
            std::size_t header_index = index + 2;
            std::string header;
            bool quoted = false;
            if (header_index < expanded.size() &&
                expanded[header_index].kind == TokenKind::String) {
                header = decode_string_literal(expanded[header_index]);
                quoted = true;
                ++header_index;
            } else if (header_index < expanded.size() && expanded[header_index].is("<")) {
                ++header_index;
                while (header_index < expanded.size() &&
                       !expanded[header_index].is(">")) {
                    header += expanded[header_index].text;
                    ++header_index;
                }
                if (header_index < expanded.size() && expanded[header_index].is(">")) {
                    ++header_index;
                }
            } else {
                diagnostics_.report(Severity::Error, directive_token.location,
                                    "expected header name in '__has_include'");
                return 0;
            }
            if (header_index >= expanded.size() || !expanded[header_index].is(")")) {
                diagnostics_.report(Severity::Error, directive_token.location,
                                    "expected ')' after '__has_include'");
                return 0;
            }
            const auto file_id = directive_token.location.file_id;
            const bool found = resolve_include(header, quoted, file_id).has_value();
            with_has_include.push_back(make_token(TokenKind::Integer,
                                                  found ? "1" : "0",
                                                  directive_token.location));
            index = header_index;
            continue;
        }
        with_has_include.push_back(expanded[index]);
    }

    IfParser parser(with_has_include, diagnostics_, directive_token.location);
    return parser.parse();
}

std::vector<Token> Preprocessor::expand(const std::vector<Token>& input,
                                        std::unordered_set<std::string> disabled,
                                        int depth) {
    if (depth > 200) {
        diagnostics_.report(Severity::Error, SourceLocation{},
                            "macro expansion exceeded the recursion limit");
        return input;
    }

    std::vector<Token> output;
    for (std::size_t index = 0; index < input.size();) {
        const Token token = input[index];
        if (token.kind != TokenKind::Identifier ||
            disabled.find(token.text) != disabled.end()) {
            output.push_back(token);
            ++index;
            continue;
        }

        if (token.text == "__LINE__") {
            output.push_back(make_token(TokenKind::Integer,
                                        std::to_string(sources_.line_column(token.location).line),
                                        token.location));
            ++index;
            continue;
        }
        if (token.text == "__FILE__") {
            const auto name = token.location.valid() ? sources_.file_name(token.location)
                                                      : std::string_view{};
            output.push_back(
                make_token(TokenKind::String, quote_string(name), token.location));
            ++index;
            continue;
        }
        if (token.text == "__COUNTER__") {
            output.push_back(make_token(TokenKind::Integer, std::to_string(counter_++),
                                        token.location));
            ++index;
            continue;
        }

        const auto macro_it = macros_.find(token.text);
        if (macro_it == macros_.end()) {
            output.push_back(token);
            ++index;
            continue;
        }

        const Macro& macro = macro_it->second;
        std::unordered_set<std::string> next_disabled = disabled;
        next_disabled.insert(macro.name);

        if (macro.function_like) {
            if (index + 1 >= input.size() || !input[index + 1].is("(")) {
                output.push_back(token);
                ++index;
                continue;
            }
            std::vector<std::vector<Token>> arguments;
            std::size_t close_index = 0;
            if (!parse_macro_arguments(input, index + 1, arguments, close_index)) {
                diagnostics_.report(Severity::Error, token.location,
                                    "unterminated macro invocation");
                output.push_back(token);
                ++index;
                continue;
            }
            if (macro.parameters.empty() && arguments.size() == 1 &&
                arguments[0].empty()) {
                arguments.clear();
            }
            if ((!macro.variadic && arguments.size() != macro.parameters.size()) ||
                (macro.variadic && arguments.size() < macro.parameters.size())) {
                diagnostics_.report(Severity::Error, token.location,
                                    "wrong number of arguments for macro '" + macro.name + "'");
                index = close_index + 1;
                continue;
            }
            // The arguments are expanded before substitution, and at that point this
            // macro is **not** yet disabled: C expands `D(D(10))` by expanding the inner
            // invocation first. Passing `next_disabled` disabled `D` too early, so the
            // inner one was substituted unexpanded and the rescan below could not
            // recover it -- the output was `( ( D ( 10 ) ) * 2 )`.
            auto replacement = substitute(macro, arguments, token, disabled);
            auto expanded = expand(replacement, next_disabled, depth + 1);
            output.insert(output.end(), std::make_move_iterator(expanded.begin()),
                          std::make_move_iterator(expanded.end()));
            index = close_index + 1;
            continue;
        }

        auto expanded = expand(macro.replacement, next_disabled, depth + 1);
        output.insert(output.end(), std::make_move_iterator(expanded.begin()),
                      std::make_move_iterator(expanded.end()));
        ++index;
    }
    return output;
}

bool Preprocessor::parse_macro_arguments(const std::vector<Token>& input,
                                         std::size_t open_index,
                                         std::vector<std::vector<Token>>& arguments,
                                         std::size_t& close_index) const {
    if (open_index >= input.size() || !input[open_index].is("(")) {
        return false;
    }

    arguments.clear();
    arguments.emplace_back();
    int depth = 0;
    for (std::size_t index = open_index + 1; index < input.size(); ++index) {
        const Token& token = input[index];
        if (token.is("(")) {
            ++depth;
            arguments.back().push_back(token);
            continue;
        }
        if (token.is(")")) {
            if (depth == 0) {
                close_index = index;
                return true;
            }
            --depth;
            arguments.back().push_back(token);
            continue;
        }
        if (token.is(",") && depth == 0) {
            arguments.emplace_back();
            continue;
        }
        arguments.back().push_back(token);
    }
    return false;
}

std::vector<Token> Preprocessor::substitute(
    const Macro& macro, const std::vector<std::vector<Token>>& arguments,
    const Token& invocation, const std::unordered_set<std::string>& disabled) {
    std::vector<Token> output;
    const auto expanded_argument = [&](std::size_t index) {
        if (index >= arguments.size()) {
            return std::vector<Token>{};
        }
        return expand(arguments[index], disabled, 0);
    };
    const auto raw_argument = [&](std::size_t index) {
        return index < arguments.size() ? arguments[index] : std::vector<Token>{};
    };
    const auto parameter_index = [&](std::string_view name) -> std::optional<std::size_t> {
        for (std::size_t index = 0; index < macro.parameters.size(); ++index) {
            if (macro.parameters[index] == name) {
                return index;
            }
        }
        return std::nullopt;
    };

    for (std::size_t index = 0; index < macro.replacement.size(); ++index) {
        const Token& token = macro.replacement[index];

        if (token.is("#") && index + 1 < macro.replacement.size() &&
            macro.replacement[index + 1].kind == TokenKind::Identifier) {
            const auto parameter = parameter_index(macro.replacement[index + 1].text);
            if (parameter) {
                output.push_back(make_token(TokenKind::String,
                                            quote_string(stringize(raw_argument(*parameter))),
                                            invocation.location));
                ++index;
                continue;
            }
        }

        if (token.is("##") && !output.empty()) {
            if (index + 1 >= macro.replacement.size()) {
                continue;
            }
            const Token& next = macro.replacement[index + 1];
            std::vector<Token> next_tokens;
            if (next.kind == TokenKind::Identifier && parameter_index(next.text)) {
                next_tokens = raw_argument(*parameter_index(next.text));
            } else {
                next_tokens.push_back(next);
            }
            if (!next_tokens.empty()) {
                Token pasted = make_token(classify_pasted(output.back().text + next_tokens[0].text),
                                          output.back().text + next_tokens[0].text,
                                          invocation.location);
                output.back() = std::move(pasted);
                for (std::size_t rest = 1; rest < next_tokens.size(); ++rest) {
                    output.push_back(next_tokens[rest]);
                }
            }
            ++index;
            continue;
        }

        if (token.kind == TokenKind::Identifier && macro.variadic &&
            token.text == "__VA_ARGS__") {
            std::vector<Token> joined;
            for (std::size_t arg = macro.parameters.size(); arg < arguments.size(); ++arg) {
                if (!joined.empty()) {
                    joined.push_back(make_token(TokenKind::Punctuator, ",", invocation.location));
                }
                auto expanded = expanded_argument(arg);
                joined.insert(joined.end(), std::make_move_iterator(expanded.begin()),
                              std::make_move_iterator(expanded.end()));
            }
            output.insert(output.end(), std::make_move_iterator(joined.begin()),
                          std::make_move_iterator(joined.end()));
            continue;
        }

        if (token.kind == TokenKind::Identifier) {
            const auto parameter = parameter_index(token.text);
            if (parameter) {
                const bool paste_left = index > 0 && macro.replacement[index - 1].is("##");
                const bool paste_right = index + 1 < macro.replacement.size() &&
                                         macro.replacement[index + 1].is("##");
                auto argument = (paste_left || paste_right) ? raw_argument(*parameter)
                                                             : expanded_argument(*parameter);
                output.insert(output.end(), std::make_move_iterator(argument.begin()),
                              std::make_move_iterator(argument.end()));
                continue;
            }
        }

        output.push_back(token);
    }
    return output;
}

std::string Preprocessor::stringize(const std::vector<Token>& tokens) const {
    std::string result;
    for (const auto& token : tokens) {
        if (!result.empty()) {
            result.push_back(' ');
        }
        result += token.text;
    }
    return result;
}

}  // namespace krc::frontend
