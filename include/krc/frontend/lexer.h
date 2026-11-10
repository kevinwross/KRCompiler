#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "krc/frontend/diagnostics.h"
#include "krc/frontend/source_manager.h"

namespace krc::frontend {

enum class TokenKind {
    Eof,
    Identifier,
    Keyword,
    Integer,
    Float,
    Char,
    String,
    Punctuator,
};

struct Token {
    TokenKind kind = TokenKind::Eof;
    std::string text;
    SourceLocation location;

    [[nodiscard]] bool is(std::string_view spelling) const { return text == spelling; }
    [[nodiscard]] bool is_keyword(std::string_view spelling) const {
        return kind == TokenKind::Keyword && text == spelling;
    }
    [[nodiscard]] bool is_eof() const { return kind == TokenKind::Eof; }
};

class Lexer {
public:
    Lexer(const SourceManager& sources, DiagnosticEngine& diagnostics, std::uint32_t file_id);

    [[nodiscard]] std::vector<Token> tokenize();

private:
    [[nodiscard]] bool at_end() const;
    [[nodiscard]] char peek(std::size_t lookahead = 0) const;
    char advance();
    void skip_whitespace_and_comments();
    [[nodiscard]] Token make_token(TokenKind kind, std::size_t start, std::size_t length) const;

    void lex_identifier(std::vector<Token>& tokens);
    void lex_number(std::vector<Token>& tokens);
    void lex_char(std::vector<Token>& tokens, std::size_t prefix_length);
    void lex_string(std::vector<Token>& tokens, std::size_t prefix_length);
    bool lex_punctuator(std::vector<Token>& tokens);

    const SourceManager& sources_;
    DiagnosticEngine& diagnostics_;
    std::uint32_t file_id_;
    std::string_view source_;
    std::size_t position_ = 0;
};

[[nodiscard]] bool is_c_keyword(std::string_view text);

}  // namespace krc::frontend
