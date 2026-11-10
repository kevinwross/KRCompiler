#include "krc/frontend/lexer.h"

#include <array>
#include <string_view>
#include <utility>

namespace krc::frontend {

namespace {

constexpr std::array<std::string_view, 61> kKeywords{
    "auto",       "break",      "case",       "char",        "class",
    "const",      "constexpr",  "continue",   "default",     "delete",
    "do",         "double",     "else",       "new",         "enum",       "extern",     "float",      "for",         "goto",
    "if",         "inline",     "int",        "long",        "register",
    "restrict",   "return",     "short",      "signed",      "sizeof",
    "static",     "struct",     "switch",     "typedef",     "union",
    "unsigned",   "void",       "volatile",   "while",       "_Alignas",
    "_Alignof",   "_Atomic",    "_Bool",      "_Complex",    "_Generic",
    "_Imaginary", "_Noreturn",  "_Static_assert", "_Thread_local", "__attribute__",
    "__declspec", "__inline",   "__restrict", "__asm__",     "__extension__",
    "typeof",     "__typeof__", "namespace",  "using",       "explicit",
    "noexcept",   "final",
};

bool is_identifier_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_identifier_char(char c) {
    return is_identifier_start(c) || (c >= '0' && c <= '9');
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool is_hex_digit(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

char digraph_first(char a, char b) {
    if (a == '<' && b == ':') {
        return '[';
    }
    if (a == ':' && b == '>') {
        return ']';
    }
    if (a == '<' && b == '%') {
        return '{';
    }
    if (a == '%' && b == '>') {
        return '}';
    }
    if (a == '%' && b == ':') {
        return '#';
    }
    return '\0';
}

}  // namespace

bool is_c_keyword(std::string_view text) {
    for (const auto keyword : kKeywords) {
        if (keyword == text) {
            return true;
        }
    }
    return false;
}

Lexer::Lexer(const SourceManager& sources, DiagnosticEngine& diagnostics, std::uint32_t file_id)
    : sources_(sources), diagnostics_(diagnostics), file_id_(file_id),
      source_(sources.file(file_id).text()) {}

bool Lexer::at_end() const { return position_ >= source_.size(); }

char Lexer::peek(std::size_t lookahead) const {
    const auto index = position_ + lookahead;
    return index < source_.size() ? source_[index] : '\0';
}

char Lexer::advance() {
    if (position_ >= source_.size()) {
        return '\0';
    }
    return source_[position_++];
}

Token Lexer::make_token(TokenKind kind, std::size_t start, std::size_t length) const {
    return Token{kind, std::string(source_.substr(start, length)),
                 SourceLocation{file_id_, static_cast<std::uint32_t>(start)}};
}

void Lexer::skip_whitespace_and_comments() {
    for (;;) {
        while (!at_end()) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
                ++position_;
                continue;
            }
            if (c == '\\' && (peek(1) == '\n' || (peek(1) == '\r' && peek(2) == '\n'))) {
                position_ += (peek(1) == '\r') ? 3 : 2;
                continue;
            }
            break;
        }

        if (position_ + 1 < source_.size() && source_[position_] == '/' &&
            source_[position_ + 1] == '/') {
            position_ += 2;
            while (!at_end() && peek() != '\n') {
                ++position_;
            }
            continue;
        }
        if (position_ + 1 < source_.size() && source_[position_] == '/' &&
            source_[position_ + 1] == '*') {
            const auto start = position_;
            position_ += 2;
            bool closed = false;
            while (!at_end()) {
                if (peek() == '*' && peek(1) == '/') {
                    position_ += 2;
                    closed = true;
                    break;
                }
                ++position_;
            }
            if (!closed) {
                diagnostics_.report(Severity::Error,
                                    sources_.location(file_id_, static_cast<std::uint32_t>(start)),
                                    "unterminated block comment");
            }
            continue;
        }
        return;
    }
}

void Lexer::lex_identifier(std::vector<Token>& tokens) {
    const auto start = position_;
    while (!at_end() && is_identifier_char(peek())) {
        ++position_;
    }
    const auto text = source_.substr(start, position_ - start);
    tokens.push_back(make_token(is_c_keyword(text) ? TokenKind::Keyword : TokenKind::Identifier,
                                start, position_ - start));
}

void Lexer::lex_number(std::vector<Token>& tokens) {
    const auto start = position_;
    bool is_float = false;

    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
        position_ += 2;
        while (!at_end() && is_hex_digit(peek())) {
            ++position_;
        }
        if (peek() == '.') {
            is_float = true;
            ++position_;
            while (!at_end() && is_hex_digit(peek())) {
                ++position_;
            }
        }
        if (peek() == 'p' || peek() == 'P') {
            is_float = true;
            ++position_;
            if (peek() == '+' || peek() == '-') {
                ++position_;
            }
            while (!at_end() && is_digit(peek())) {
                ++position_;
            }
        }
    } else {
        while (!at_end() && is_digit(peek())) {
            ++position_;
        }
        if (peek() == '.') {
            is_float = true;
            ++position_;
            while (!at_end() && is_digit(peek())) {
                ++position_;
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            is_float = true;
            ++position_;
            if (peek() == '+' || peek() == '-') {
                ++position_;
            }
            while (!at_end() && is_digit(peek())) {
                ++position_;
            }
        }
    }

    while (!at_end()) {
        const char c = peek();
        if (c == 'u' || c == 'U' || c == 'l' || c == 'L' || c == 'f' || c == 'F') {
            if (c == 'f' || c == 'F') {
                is_float = true;
            }
            ++position_;
            continue;
        }
        break;
    }

    tokens.push_back(
        make_token(is_float ? TokenKind::Float : TokenKind::Integer, start, position_ - start));
}

void Lexer::lex_char(std::vector<Token>& tokens, std::size_t prefix_length) {
    const auto start = position_;
    position_ += prefix_length;  // prefix
    ++position_;                 // opening quote
    bool closed = false;
    while (!at_end()) {
        const char c = peek();
        if (c == '\\') {
            ++position_;
            if (!at_end()) {
                ++position_;
            }
            continue;
        }
        if (c == '\'') {
            ++position_;
            closed = true;
            break;
        }
        if (c == '\n') {
            break;
        }
        ++position_;
    }
    if (!closed) {
        diagnostics_.report(Severity::Error,
                            sources_.location(file_id_, static_cast<std::uint32_t>(start)),
                            "unterminated character constant");
    }
    tokens.push_back(make_token(TokenKind::Char, start, position_ - start));
}

void Lexer::lex_string(std::vector<Token>& tokens, std::size_t prefix_length) {
    const auto start = position_;
    position_ += prefix_length;
    ++position_;  // opening quote
    bool closed = false;
    while (!at_end()) {
        const char c = peek();
        if (c == '\\') {
            ++position_;
            if (!at_end()) {
                ++position_;
            }
            continue;
        }
        if (c == '"') {
            ++position_;
            closed = true;
            break;
        }
        if (c == '\n') {
            break;
        }
        ++position_;
    }
    if (!closed) {
        diagnostics_.report(Severity::Error,
                            sources_.location(file_id_, static_cast<std::uint32_t>(start)),
                            "unterminated string literal");
    }
    tokens.push_back(make_token(TokenKind::String, start, position_ - start));
}

bool Lexer::lex_punctuator(std::vector<Token>& tokens) {
    const auto start = position_;
    const char first = peek();
    const char second = peek(1);
    const char third = peek(2);

    if ((first == '<' && second == '<' && third == '=') ||
        (first == '>' && second == '>' && third == '=') ||
        (first == '.' && second == '.' && third == '.')) {
        position_ += 3;
        tokens.push_back(make_token(TokenKind::Punctuator, start, 3));
        return true;
    }

    const std::string_view two = source_.substr(position_, 2);
    if (two == "->" || two == "++" || two == "--" || two == "<<" || two == ">>" ||
        two == "<=" || two == ">=" || two == "==" || two == "!=" || two == "&&" ||
        two == "||" || two == "*=" || two == "/=" || two == "%=" || two == "+=" ||
        two == "-=" || two == "&=" || two == "|=" || two == "^=" || two == "##") {
        position_ += 2;
        tokens.push_back(make_token(TokenKind::Punctuator, start, 2));
        return true;
    }

    if (first == '%' && second == ':' && third == '%' && peek(3) == ':') {
        position_ += 4;
        tokens.push_back(Token{TokenKind::Punctuator, "##",
                               SourceLocation{file_id_, static_cast<std::uint32_t>(start)}});
        return true;
    }

    if (first == ':' && second == ':') {
        position_ += 2;
        tokens.push_back(Token{TokenKind::Punctuator, "::",
                               SourceLocation{file_id_, static_cast<std::uint32_t>(start)}});
        return true;
    }

    const char mapped = digraph_first(first, second);
    if (mapped != '\0') {
        position_ += 2;
        Token token{TokenKind::Punctuator, std::string(1, mapped),
                    SourceLocation{file_id_, static_cast<std::uint32_t>(start)}};
        tokens.push_back(std::move(token));
        return true;
    }

    constexpr std::string_view kSingle = "[](){}.,;:?~!+-*/%<>=&|^#";
    if (kSingle.find(first) != std::string_view::npos) {
        ++position_;
        tokens.push_back(make_token(TokenKind::Punctuator, start, 1));
        return true;
    }
    return false;
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (!at_end()) {
        skip_whitespace_and_comments();
        if (at_end()) {
            break;
        }

        const auto start = position_;
        const char c = peek();
        if (is_identifier_start(c)) {
            const bool string_prefix =
                (c == 'u' && peek(1) == '8' && (peek(2) == '"' || peek(2) == '\'')) ||
                ((c == 'u' || c == 'U' || c == 'L') && (peek(1) == '"' || peek(1) == '\''));
            if (string_prefix) {
                const std::size_t prefix = (c == 'u' && peek(1) == '8') ? 2 : 1;
                if (peek(prefix) == '"') {
                    lex_string(tokens, prefix);
                } else {
                    lex_char(tokens, prefix);
                }
                continue;
            }
            lex_identifier(tokens);
            continue;
        }
        if (is_digit(c) || (c == '.' && is_digit(peek(1)))) {
            lex_number(tokens);
            continue;
        }
        if (c == '"') {
            lex_string(tokens, 0);
            continue;
        }
        if (c == '\'') {
            lex_char(tokens, 0);
            continue;
        }
        if (lex_punctuator(tokens)) {
            continue;
        }

        diagnostics_.report(Severity::Error,
                            sources_.location(file_id_, static_cast<std::uint32_t>(start)),
                            "unexpected character in source");
        ++position_;
    }

    tokens.push_back(Token{TokenKind::Eof, {},
                           SourceLocation{file_id_,
                                          static_cast<std::uint32_t>(source_.size())}});
    return tokens;
}

}  // namespace krc::frontend
