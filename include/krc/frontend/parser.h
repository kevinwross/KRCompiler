#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "krc/frontend/ast.h"
#include "krc/frontend/diagnostics.h"
#include "krc/frontend/lexer.h"
#include "krc/frontend/source_manager.h"

namespace krc::frontend {

class Parser {
public:
    Parser(std::vector<Token> tokens, DiagnosticEngine& diagnostics,
           const SourceManager& sources, bool cxx_mode = false);

    [[nodiscard]] std::unique_ptr<AstNode> parse_translation_unit();
    [[nodiscard]] bool had_error() const { return had_error_; }

private:
    [[nodiscard]] const Token& peek(std::size_t lookahead = 0) const;
    [[nodiscard]] bool at_end() const;
    [[nodiscard]] bool check(std::string_view spelling) const;
    [[nodiscard]] bool check_keyword(std::string_view spelling) const;
    bool match(std::string_view spelling);
    bool expect(std::string_view spelling, std::string_view context);
    void error(const Token& token, std::string message);
    void synchronize();

    [[nodiscard]] std::unique_ptr<AstNode> parse_external_declaration();
    [[nodiscard]] std::unique_ptr<AstNode> parse_declaration();
    [[nodiscard]] std::unique_ptr<AstNode> parse_decl_specifiers();
    [[nodiscard]] std::unique_ptr<AstNode> parse_declarator(bool allow_abstract);
    [[nodiscard]] std::unique_ptr<AstNode> parse_initializer();
    [[nodiscard]] std::unique_ptr<AstNode> parse_initializer_list();
    [[nodiscard]] std::unique_ptr<AstNode> parse_compound_statement();
    [[nodiscard]] std::unique_ptr<AstNode> parse_statement();
    [[nodiscard]] std::unique_ptr<AstNode> parse_struct_specifier(bool is_union);
    [[nodiscard]] std::unique_ptr<AstNode> parse_enum_specifier();
    [[nodiscard]] std::unique_ptr<AstNode> parse_member_declaration();
    [[nodiscard]] std::unique_ptr<AstNode> parse_static_assert();
    [[nodiscard]] std::unique_ptr<AstNode> parse_expression();
    [[nodiscard]] std::unique_ptr<AstNode> parse_assignment_expression();
    [[nodiscard]] std::unique_ptr<AstNode> parse_conditional_expression();
    [[nodiscard]] std::unique_ptr<AstNode> parse_binary_expression(int minimum_precedence);
    [[nodiscard]] std::unique_ptr<AstNode> parse_unary_expression();
    [[nodiscard]] std::unique_ptr<AstNode> parse_postfix_expression();
    // Applies the postfix operators (subscript, call, member, increment) to an
    // already-parsed expression. Cast and `typeid` results use it so that
    // `static_cast<T>(x).m` and `typeid(x).name()` parse.
    [[nodiscard]] std::unique_ptr<AstNode> parse_postfix_tail(
        std::unique_ptr<AstNode> expression);
    [[nodiscard]] std::unique_ptr<AstNode> parse_primary_expression();
    [[nodiscard]] std::unique_ptr<AstNode> parse_argument_list();
    [[nodiscard]] std::unique_ptr<AstNode> parse_type_name();
    [[nodiscard]] std::unique_ptr<AstNode> parse_generic_selection();
    [[nodiscard]] std::unique_ptr<AstNode> parse_lambda_expression();
    [[nodiscard]] std::unique_ptr<AstNode> parse_offsetof_expression();

    [[nodiscard]] bool is_declaration_start() const;
    [[nodiscard]] bool is_type_name_start() const;
    [[nodiscard]] bool is_typedef_name(std::string_view name) const;
    [[nodiscard]] bool is_assignment_operator(std::string_view spelling) const;
    [[nodiscard]] int precedence(std::string_view spelling) const;

    void skip_balanced_parens();
    void skip_attributes();
    [[nodiscard]] std::string declarator_name(const AstNode& declarator) const;
    void register_typedefs(const AstNode& specifiers, const AstNode& declaration);

    std::vector<Token> tokens_;
    DiagnosticEngine& diagnostics_;
    const SourceManager& sources_;
    std::size_t position_ = 0;
    std::unordered_set<std::string> typedef_names_;
    std::unordered_set<std::string> class_names_;
    std::string current_class_name_;
    bool specifiers_have_typedef_ = false;
    bool cxx_mode_ = false;
    bool had_error_ = false;
};

}  // namespace krc::frontend
