#include <string>
#include <vector>

#include "doctest.h"
#include "krc/frontend/diagnostics.h"
#include "krc/frontend/lexer.h"
#include "krc/frontend/source_manager.h"

using krc::frontend::DiagnosticEngine;
using krc::frontend::Lexer;
using krc::frontend::SourceManager;
using krc::frontend::Token;
using krc::frontend::TokenKind;

namespace {

struct Lexed {
    std::vector<Token> tokens;
    bool had_errors = false;
};

Lexed lex(std::string source) {
    SourceManager sources;
    DiagnosticEngine diagnostics(sources);
    const auto file = sources.add_buffer("test.c", std::move(source));
    Lexer lexer(sources, diagnostics, file);
    return Lexed{lexer.tokenize(), diagnostics.has_errors()};
}

}  // namespace

TEST_CASE("lexer recognizes keywords, identifiers, and punctuation") {
    const auto result = lex("int main(void) { return 0; }");
    REQUIRE(result.tokens.size() == 11);
    CHECK(result.tokens[0].kind == TokenKind::Keyword);
    CHECK(result.tokens[0].text == "int");
    CHECK(result.tokens[1].kind == TokenKind::Identifier);
    CHECK(result.tokens[1].text == "main");
    CHECK(result.tokens[2].text == "(");
    CHECK(result.tokens[3].text == "void");
    CHECK(result.tokens[6].text == "return");
    CHECK(result.tokens[7].kind == TokenKind::Integer);
    CHECK(result.tokens[7].text == "0");
    CHECK(result.tokens[9].text == "}");
    CHECK(result.tokens[10].kind == TokenKind::Eof);
    CHECK_FALSE(result.had_errors);
}

TEST_CASE("lexer recognizes numeric, character, and string literals") {
    const auto result = lex("0x10 42 1.5f 1e3 'a' \"hi\\n\"");
    REQUIRE(result.tokens.size() == 7);
    CHECK(result.tokens[0].kind == TokenKind::Integer);
    CHECK(result.tokens[0].text == "0x10");
    CHECK(result.tokens[1].kind == TokenKind::Integer);
    CHECK(result.tokens[2].kind == TokenKind::Float);
    CHECK(result.tokens[2].text == "1.5f");
    CHECK(result.tokens[3].kind == TokenKind::Float);
    CHECK(result.tokens[4].kind == TokenKind::Char);
    CHECK(result.tokens[5].kind == TokenKind::String);
    CHECK_FALSE(result.had_errors);
}

TEST_CASE("lexer skips comments and line splices") {
    const auto result = lex("int /* block */ main // line\n (void) { return 0; }");
    REQUIRE_FALSE(result.tokens.empty());
    CHECK(result.tokens[0].text == "int");
    CHECK(result.tokens[1].text == "main");
    CHECK(result.tokens[2].text == "(");
    CHECK_FALSE(result.had_errors);

    const auto spliced = lex("in\\\nt main(void) { return 0; }");
    REQUIRE_FALSE(spliced.tokens.empty());
    CHECK(spliced.tokens[0].text == "in");
    CHECK(spliced.tokens[1].text == "t");
}

TEST_CASE("lexer maps digraphs") {
    const auto result = lex("<: :> <% %> %: %:%:");
    REQUIRE(result.tokens.size() == 7);
    CHECK(result.tokens[0].text == "[");
    CHECK(result.tokens[1].text == "]");
    CHECK(result.tokens[2].text == "{");
    CHECK(result.tokens[3].text == "}");
    CHECK(result.tokens[4].text == "#");
    CHECK(result.tokens[5].text == "##");
}

TEST_CASE("lexer diagnoses unterminated literals") {
    const auto string_result = lex("\"unterminated");
    CHECK(string_result.had_errors);

    const auto comment_result = lex("/* unterminated");
    CHECK(comment_result.had_errors);
}
