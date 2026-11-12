#include "krc/frontend/parser.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace krc::frontend {

namespace {

bool is_storage_specifier(std::string_view text) {
    return text == "typedef" || text == "extern" || text == "static" || text == "auto" ||
           text == "register" || text == "_Thread_local";
}

bool is_function_specifier(std::string_view text) {
    return text == "inline" || text == "_Noreturn" || text == "__inline" ||
           text == "constexpr" || text == "explicit" || text == "noexcept" ||
           text == "final";
}

bool is_qualifier(std::string_view text) {
    return text == "const" || text == "volatile" || text == "restrict" ||
           text == "__restrict" || text == "_Atomic";
}

bool is_type_specifier(std::string_view text) {
    return text == "void" || text == "char" || text == "short" || text == "int" ||
           text == "long" || text == "float" || text == "double" || text == "signed" ||
           text == "unsigned" || text == "_Bool" || text == "_Complex" ||
           text == "_Imaginary" || text == "struct" || text == "union" || text == "enum";
}

bool is_decl_specifier_keyword(std::string_view text) {
    return is_storage_specifier(text) || is_function_specifier(text) ||
           is_qualifier(text) || is_type_specifier(text) || text == "_Alignas";
}
bool contains_function_declarator(const AstNode& declarator) {
    if (declarator.kind == AstKind::FunctionDeclarator) {
        return true;
    }
    for (const auto& child : declarator.children) {
        if (child && contains_function_declarator(*child)) {
            return true;
        }
    }
    return false;
}

}  // namespace

Parser::Parser(std::vector<Token> tokens, DiagnosticEngine& diagnostics,
               const SourceManager& sources, bool cxx_mode)
    : tokens_(std::move(tokens)), diagnostics_(diagnostics), sources_(sources),
      cxx_mode_(cxx_mode) {}

const Token& Parser::peek(std::size_t lookahead) const {
    static const Token eof{TokenKind::Eof, {}, SourceLocation{}};
    const auto index = position_ + lookahead;
    return index < tokens_.size() ? tokens_[index] : eof;
}

bool Parser::at_end() const { return peek().is_eof(); }

bool Parser::check(std::string_view spelling) const { return peek().text == spelling; }

bool Parser::check_keyword(std::string_view spelling) const {
    return peek().kind == TokenKind::Keyword && peek().text == spelling;
}

bool Parser::match(std::string_view spelling) {
    if (!check(spelling)) {
        return false;
    }
    ++position_;
    return true;
}

bool Parser::expect(std::string_view spelling, std::string_view context) {
    if (match(spelling)) {
        return true;
    }
    error(peek(), "expected '" + std::string(spelling) + "' " + std::string(context));
    return false;
}

void Parser::error(const Token& token, std::string message) {
    had_error_ = true;
    diagnostics_.report(Severity::Error, token.location, std::move(message));
}

void Parser::synchronize() {
    while (!at_end()) {
        if (match(";")) {
            return;
        }
        if (check("}")) {
            return;
        }
        ++position_;
    }
}

void Parser::skip_balanced_parens() {
    if (!match("(")) {
        return;
    }
    int depth = 1;
    while (!at_end() && depth > 0) {
        if (match("(")) {
            ++depth;
        } else if (match(")")) {
            --depth;
        } else {
            ++position_;
        }
    }
}

void Parser::skip_attributes() {
    while (check("__attribute__") || check("__declspec") || check("__asm__")) {
        ++position_;
        if (check("(")) {
            skip_balanced_parens();
        }
    }
}

std::unique_ptr<AstNode> Parser::parse_translation_unit() {
    auto root = make_ast(AstKind::TranslationUnit, peek().location);
    while (!at_end()) {
        auto declaration = parse_external_declaration();
        if (declaration) {
            if (declaration->kind == AstKind::TranslationUnit) {
                for (auto& child : declaration->children) {
                    root->children.push_back(std::move(child));
                }
            } else {
                root->children.push_back(std::move(declaration));
            }
        } else {
            synchronize();
            if (!at_end()) {
                ++position_;
            }
        }
    }
    return root;
}

std::unique_ptr<AstNode> Parser::parse_external_declaration() {
    if ((check("_Static_assert") || (cxx_mode_ && check("static_assert")))) {
        return parse_static_assert();
    }
    if (cxx_mode_ && check("template")) {
        const auto location = peek().location;
        ++position_;
        std::vector<std::string> parameters;
        if (match("<")) {
            while (!check(">") && !at_end()) {
                if (peek().kind == TokenKind::Keyword && peek().text == "class") {
                    ++position_;
                } else if (peek().kind == TokenKind::Identifier &&
                           (peek().text == "typename" || peek().text == "class")) {
                    ++position_;
                }
                if (peek().kind == TokenKind::Identifier) {
                    parameters.push_back(peek().text);
                    typedef_names_.insert(peek().text);
                    ++position_;
                } else {
                    ++position_;
                    continue;
                }
                if (!match(",")) {
                    break;
                }
            }
            expect(">", "after template parameter list");
        }
        auto block = make_ast(AstKind::TranslationUnit, location);
        for (const auto& parameter : parameters) {
            auto specifiers = make_ast(AstKind::DeclSpecifiers, location);
            specifiers->labels.push_back("typedef");
            specifiers->labels.push_back("int");
            auto declaration = make_ast(AstKind::Declaration, location);
            declaration->children.push_back(std::move(specifiers));
            auto init = make_ast(AstKind::InitDeclarator, location, parameter);
            init->children.push_back(make_ast(AstKind::Declarator, location, parameter));
            declaration->children.push_back(std::move(init));
            block->children.push_back(std::move(declaration));
        }
        auto body = parse_external_declaration();
        if (body) {
            if (body->kind == AstKind::TranslationUnit) {
                for (auto& child : body->children) {
                    block->children.push_back(std::move(child));
                }
            } else {
                block->children.push_back(std::move(body));
            }
        }
        return block;
    }
    if (cxx_mode_ && check("namespace")) {
        const auto location = peek().location;
        ++position_;
        if (peek().kind == TokenKind::Identifier) {
            ++position_;
        }
        auto block = make_ast(AstKind::TranslationUnit, location);
        expect("{", "after namespace");
        while (!check("}") && !at_end()) {
            auto declaration = parse_external_declaration();
            if (declaration) {
                if (declaration->kind == AstKind::TranslationUnit) {
                    for (auto& child : declaration->children) {
                        block->children.push_back(std::move(child));
                    }
                } else {
                    block->children.push_back(std::move(declaration));
                }
            } else {
                synchronize();
                if (!at_end()) {
                    ++position_;
                }
            }
        }
        expect("}", "after namespace body");
        return block;
    }
    if (cxx_mode_ && check("using")) {
        const auto location = peek().location;
        ++position_;
        if (check("namespace")) {
            ++position_;
            while (!check(";") && !at_end()) {
                ++position_;
            }
            expect(";", "after using-directive");
            return make_ast(AstKind::Declaration, location);
        }
        if (peek().kind != TokenKind::Identifier) {
            error(peek(), "expected alias name after 'using'");
            return nullptr;
        }
        const auto name = peek().text;
        ++position_;
        expect("=", "after using alias name");
        auto type = parse_type_name();
        expect(";", "after using alias");
        auto specifiers = make_ast(AstKind::DeclSpecifiers, location);
        specifiers->labels.push_back("typedef");
        auto declaration = make_ast(AstKind::Declaration, location);
        if (type) {
            specifiers->children.push_back(std::move(type));
        }
        declaration->children.push_back(std::move(specifiers));
        auto declarator = make_ast(AstKind::Declarator, location, name);
        auto init = make_ast(AstKind::InitDeclarator, location, name);
        init->children.push_back(std::move(declarator));
        declaration->children.push_back(std::move(init));
        register_typedefs(*declaration->children.front(), *declaration);
        return declaration;
    }
    return parse_declaration();
}

std::unique_ptr<AstNode> Parser::parse_declaration() {
    if ((check("_Static_assert") || (cxx_mode_ && check("static_assert")))) {
        return parse_static_assert();
    }

    auto specifiers = parse_decl_specifiers();
    if (!specifiers) {
        if (cxx_mode_ && peek().kind == TokenKind::Identifier &&
            peek(1).is("::") && peek(2).kind == TokenKind::Identifier) {
            specifiers = make_ast(AstKind::DeclSpecifiers, peek().location);
        } else {
            error(peek(), "expected declaration specifiers");
            return nullptr;
        }
    }

    if (cxx_mode_ && check("[")) {
        const auto location = peek().location;
        ++position_;
        auto binding = make_ast(AstKind::StructuredBinding, location);
        while (!check("]") && !at_end()) {
            if (peek().kind != TokenKind::Identifier) {
                error(peek(), "expected structured binding name");
                break;
            }
            binding->children.push_back(
                make_ast(AstKind::Identifier, peek().location, peek().text));
            ++position_;
            if (!match(",")) {
                break;
            }
        }
        expect("]", "after structured binding names");
        expect("=", "after structured binding");
        auto initializer = parse_initializer();
        if (initializer) {
            binding->children.push_back(std::move(initializer));
        }
        expect(";", "after structured binding");
        auto declaration = make_ast(AstKind::Declaration, location);
        declaration->children.push_back(std::move(specifiers));
        declaration->children.push_back(std::move(binding));
        return declaration;
    }

    if (match(";")) {
        auto declaration = make_ast(AstKind::Declaration, specifiers->location);
        declaration->children.push_back(std::move(specifiers));
        return declaration;
    }

    if (peek().kind == TokenKind::Identifier && peek(1).text == "(" &&
        peek(2).kind != TokenKind::Keyword && peek(2).kind != TokenKind::Eof &&
        peek(2).text != ")" &&
        std::any_of(specifiers->labels.begin(), specifiers->labels.end(),
                    [&](const std::string& label) {
                        return class_names_.contains(label);
                    })) {
        auto declaration = make_ast(AstKind::Declaration, specifiers->location);
        declaration->children.push_back(std::move(specifiers));
        auto init = make_ast(AstKind::InitDeclarator, peek().location, peek().text);
        auto declarator = make_ast(AstKind::Declarator, peek().location, peek().text);
        ++position_;
        ++position_;  // (
        init->children.push_back(std::move(declarator));
        init->children.push_back(parse_argument_list());
        declaration->children.push_back(std::move(init));
        expect(";", "after constructor-initialized declaration");
        return declaration;
    }

    auto first_declarator = parse_declarator(false);
    if (!first_declarator) {
        return nullptr;
    }

    skip_attributes();
    if (contains_function_declarator(*first_declarator) &&
        (check("{") || check(":"))) {
        std::unique_ptr<AstNode> member_initializers;
        if (match(":")) {
            member_initializers =
                make_ast(AstKind::MemberInitializerList, peek().location);
            while (!check("{") && !at_end()) {
                if (peek().kind != TokenKind::Identifier) {
                    error(peek(), "expected member initializer");
                    break;
                }
                auto initializer =
                    make_ast(AstKind::MemberInitializer, peek().location, peek().text);
                ++position_;
                if (match("(")) {
                    initializer->children.push_back(parse_argument_list());
                }
                member_initializers->children.push_back(std::move(initializer));
                if (!match(",")) {
                    break;
                }
            }
        }
        auto body = parse_compound_statement();
        auto definition = make_ast(AstKind::FunctionDefinition, specifiers->location,
                                   declarator_name(*first_declarator));
        definition->children.push_back(std::move(specifiers));
        definition->children.push_back(std::move(first_declarator));
        definition->children.push_back(std::move(body));
        if (member_initializers) {
            definition->children.push_back(std::move(member_initializers));
        }
        return definition;
    }

    auto declaration = make_ast(AstKind::Declaration, specifiers->location);
    declaration->children.push_back(std::move(specifiers));

    auto add_declarator = [&](std::unique_ptr<AstNode> declarator) {
        auto init = make_ast(AstKind::InitDeclarator, declarator->location,
                             declarator_name(*declarator));
        init->children.push_back(std::move(declarator));
        if (match("=")) {
            auto initializer = parse_initializer();
            if (cxx_mode_ && initializer &&
                initializer->kind == AstKind::CallExpression &&
                initializer->children.size() >= 2 && initializer->children[0] &&
                initializer->children[0]->kind == AstKind::Identifier &&
                initializer->children[1] &&
                declaration->children.front()) {
                const auto& labels = declaration->children.front()->labels;
                const auto callee = initializer->children[0]->text;
                const bool names_type =
                    std::any_of(labels.begin(), labels.end(),
                                [&](const std::string& label) {
                                    return label == callee &&
                                           (class_names_.contains(label) ||
                                            is_typedef_name(label));
                                });
                if (names_type) {
                    auto arguments = make_ast(AstKind::ExpressionStatement,
                                              initializer->children[1]->location);
                    arguments->children =
                        std::move(initializer->children[1]->children);
                    initializer = std::move(arguments);
                }
            }
            if (initializer) {
                init->children.push_back(std::move(initializer));
            }
        } else if (cxx_mode_ && check("{") &&
                   declaration->children.front() &&
                   std::any_of(declaration->children.front()->labels.begin(),
                               declaration->children.front()->labels.end(),
                               [&](const std::string& label) {
                                   return class_names_.contains(label) ||
                                          is_typedef_name(label);
                               })) {
            ++position_;
            auto arguments = make_ast(AstKind::ExpressionStatement, peek().location);
            while (!check("}") && !at_end()) {
                auto argument = parse_assignment_expression();
                if (argument) {
                    arguments->children.push_back(std::move(argument));
                }
                if (!match(",")) {
                    break;
                }
            }
            expect("}", "after braced initializer");
            init->children.push_back(std::move(arguments));
        }
        declaration->children.push_back(std::move(init));
    };

    add_declarator(std::move(first_declarator));
    while (match(",")) {
        auto declarator = parse_declarator(false);
        if (!declarator) {
            break;
        }
        add_declarator(std::move(declarator));
    }
    expect(";", "after declaration");
    register_typedefs(*declaration->children.front(), *declaration);
    return declaration;
}

std::unique_ptr<AstNode> Parser::parse_decl_specifiers() {
    specifiers_have_typedef_ = false;
    auto node = make_ast(AstKind::DeclSpecifiers, peek().location);
    bool saw_specifier = false;

    for (;;) {
        skip_attributes();
        if (at_end()) {
            break;
        }
        const Token& token = peek();
        if (token.kind == TokenKind::Keyword && token.text == "__extension__") {
            ++position_;
            continue;
        }
        if (token.kind == TokenKind::Keyword && token.text == "_Atomic" && peek(1).is("(")) {
            ++position_;
            ++position_;
            auto atomic_type = parse_type_name();
            if (atomic_type) {
                node->children.push_back(std::move(atomic_type));
            }
            node->labels.emplace_back("_Atomic");
            expect(")", "after _Atomic type");
            saw_specifier = true;
            continue;
        }
        if (token.kind == TokenKind::Keyword &&
            (token.text == "typeof" || token.text == "__typeof__")) {
            error(token, "typeof is not supported");
            ++position_;
            if (check("(")) {
                skip_balanced_parens();
            }
            saw_specifier = true;
            continue;
        }
        if (token.kind == TokenKind::Keyword && token.text == "struct") {
            node->children.push_back(parse_struct_specifier(false));
            saw_specifier = true;
            continue;
        }
        if (token.kind == TokenKind::Keyword && token.text == "class") {
            node->children.push_back(parse_struct_specifier(false));
            saw_specifier = true;
            continue;
        }
        if (token.kind == TokenKind::Keyword && token.text == "union") {
            node->children.push_back(parse_struct_specifier(true));
            saw_specifier = true;
            continue;
        }
        if (token.kind == TokenKind::Keyword && token.text == "enum") {
            node->children.push_back(parse_enum_specifier());
            saw_specifier = true;
            continue;
        }
        if (token.kind == TokenKind::Keyword && token.text == "_Alignas") {
            ++position_;
            auto alignment_specifier =
                make_ast(AstKind::AlignasSpecifier, token.location);
            expect("(", "after '_Alignas'");
            if (is_type_name_start()) {
                alignment_specifier->children.push_back(parse_type_name());
            } else {
                alignment_specifier->children.push_back(parse_assignment_expression());
            }
            expect(")", "after '_Alignas'");
            node->children.push_back(std::move(alignment_specifier));
            saw_specifier = true;
            continue;
        }
        if (token.kind == TokenKind::Keyword && is_decl_specifier_keyword(token.text)) {
            if (token.text == "typedef") {
                specifiers_have_typedef_ = true;
            }
            node->labels.push_back(token.text);
            ++position_;
            saw_specifier = true;
            continue;
        }
        if (cxx_mode_ && token.kind == TokenKind::Identifier &&
            token.text == "decltype" && peek(1).is("(")) {
            ++position_;
            ++position_;
            auto type = make_ast(AstKind::TypeName, token.location, "decltype");
            auto expression = parse_expression();
            if (expression) {
                type->children.push_back(std::move(expression));
            }
            expect(")", "after 'decltype'");
            node->children.push_back(std::move(type));
            saw_specifier = true;
            continue;
        }
        if (cxx_mode_ && token.kind == TokenKind::Identifier &&
            token.text == "alignas" && peek(1).is("(")) {
            ++position_;
            auto alignment_specifier = make_ast(AstKind::AlignasSpecifier, token.location);
            expect("(", "after 'alignas'");
            if (is_type_name_start()) {
                alignment_specifier->children.push_back(parse_type_name());
            } else {
                alignment_specifier->children.push_back(parse_assignment_expression());
            }
            expect(")", "after 'alignas'");
            node->children.push_back(std::move(alignment_specifier));
            saw_specifier = true;
            continue;
        }
        if (cxx_mode_ && token.kind == TokenKind::Identifier &&
            (token.text == "mutable" || token.text == "explicit")) {
            node->labels.push_back(token.text);
            ++position_;
            saw_specifier = true;
            continue;
        }
        if (cxx_mode_ && token.kind == TokenKind::Identifier &&
            peek(1).is("::") && peek(2).kind == TokenKind::Identifier &&
            is_typedef_name(peek(2).text) && peek(2).text != token.text) {
            node->labels.push_back(peek(2).text);
            position_ += 3;
            saw_specifier = true;
            continue;
        }
        if (token.kind == TokenKind::Identifier && is_typedef_name(token.text) &&
            !(cxx_mode_ && peek(1).is("::"))) {
            node->labels.push_back(token.text);
            ++position_;
            saw_specifier = true;
            continue;
        }
        break;
    }

    return saw_specifier ? std::move(node) : nullptr;
}

bool Parser::is_typedef_name(std::string_view name) const {
    return typedef_names_.find(std::string(name)) != typedef_names_.end();
}

std::unique_ptr<AstNode> Parser::parse_declarator(bool allow_abstract) {
    int pointer_count = 0;
    bool is_reference = false;
    bool is_rvalue_reference = false;
    while (match("*")) {
        ++pointer_count;
        while (check("const") || check("volatile") || check("restrict") || check("_Atomic") ||
               check("__restrict")) {
            ++position_;
        }
    }
    if (match("&")) {
        is_reference = true;
        if (check("&")) {
            ++position_;
            is_rvalue_reference = true;
        }
    } else if (match("&&")) {
        is_reference = true;
        is_rvalue_reference = true;
    }

    std::unique_ptr<AstNode> direct;
    if (peek().kind == TokenKind::Identifier && peek().text == "operator") {
        const auto location = peek().location;
        ++position_;
        std::string name = "operator";
        if (peek().is("[") && peek(1).is("]")) {
            name += "[]";
            position_ += 2;
        } else if (peek().is("(") && peek(1).is(")")) {
            name += "()";
            position_ += 2;
        } else if (peek().kind == TokenKind::Punctuator) {
            name += peek().text;
            ++position_;
        }
        direct = make_ast(AstKind::Declarator, location, std::move(name));
    } else if (peek().kind == TokenKind::Identifier) {
        std::string name = peek().text;
        const auto location = peek().location;
        ++position_;
        if (cxx_mode_) {
            while (check("::") && peek(1).kind == TokenKind::Identifier) {
                ++position_;
                name += "::";
                name += peek().text;
                ++position_;
            }
        }
        direct = make_ast(AstKind::Declarator, location, std::move(name));
    } else if (match("(")) {
        direct = parse_declarator(allow_abstract);
        expect(")", "after parenthesized declarator");
    } else if (allow_abstract) {
        direct = make_ast(AstKind::Declarator, peek().location);
    } else {
        error(peek(), "expected declarator");
        return nullptr;
    }

    for (;;) {
        if (match("[")) {
            std::unique_ptr<AstNode> size;
            if (!check("]")) {
                if (check("static") || check("const") || check("volatile")) {
                    ++position_;
                }
                if (match("*")) {
                    size = make_ast(AstKind::IntegerLiteral, peek().location, "*");
                } else {
                    size = parse_assignment_expression();
                }
            }
            expect("]", "after array bound");
            auto array = make_ast(AstKind::ArrayDeclarator, direct->location);
            array->children.push_back(std::move(direct));
            if (size) {
                array->children.push_back(std::move(size));
            }
            direct = std::move(array);
            continue;
        }
        if (match("(")) {
            auto function = make_ast(AstKind::FunctionDeclarator, direct->location);
            function->children.push_back(std::move(direct));
            bool has_prototype = false;
            if (!check(")")) {
                has_prototype = true;
                if (check("void") && peek(1).text == ")") {
                    ++position_;
                } else {
                    for (;;) {
                        if (match("...")) {
                            function->labels.emplace_back("variadic");
                            break;
                        }
                        if (peek().kind == TokenKind::Identifier &&
                            (peek(1).is(",") || peek(1).is(")"))) {
                            error(peek(),
                                  "K&R function definitions are not supported; "
                                  "declare each parameter type");
                            ++position_;
                            if (match(",")) {
                                continue;
                            }
                            break;
                        }
                        auto parameter = make_ast(AstKind::ParameterDeclaration, peek().location);
                        auto parameter_specifiers = parse_decl_specifiers();
                        if (!parameter_specifiers) {
                            error(peek(), "expected parameter declaration");
                            break;
                        }
                        parameter->children.push_back(std::move(parameter_specifiers));
                        if (!check(",") && !check(")")) {
                            auto parameter_declarator = parse_declarator(true);
                            if (parameter_declarator) {
                                parameter->children.push_back(std::move(parameter_declarator));
                            }
                            if (match("=")) {
                                auto default_value = parse_assignment_expression();
                                if (default_value) {
                                    parameter->children.push_back(std::move(default_value));
                                }
                            }
                        }
                        function->children.push_back(std::move(parameter));
                        if (match(",")) {
                            continue;
                        }
                        break;
                    }
                }
            }
            if (has_prototype) {
                function->labels.emplace_back("prototype");
            }
            expect(")", "after parameter list");
            direct = std::move(function);
            continue;
        }
        if (cxx_mode_ &&
            (check("const") || check("volatile") || check("noexcept") ||
             check("override") || check("final") || check("&") || check("&&"))) {
            const bool is_noexcept = check("noexcept");
            ++position_;
            if (is_noexcept && check("(")) {
                skip_balanced_parens();
            }
            continue;
        }
        break;
    }

    for (int index = 0; index < pointer_count; ++index) {
        auto pointer = make_ast(AstKind::PointerDeclarator, direct->location);
        pointer->children.push_back(std::move(direct));
        direct = std::move(pointer);
    }
    if (is_reference) {
        auto reference = make_ast(AstKind::ReferenceDeclarator, direct->location);
        if (is_rvalue_reference) {
            reference->labels.push_back("rvalue");
        }
        reference->children.push_back(std::move(direct));
        direct = std::move(reference);
    }
    return direct;
}

std::unique_ptr<AstNode> Parser::parse_struct_specifier(bool is_union) {
    const auto location = peek().location;
    const bool is_class = peek().text == "class";
    ++position_;  // struct, class, or union
    auto node = make_ast(is_union ? AstKind::UnionSpecifier : AstKind::StructSpecifier, location);
    if (is_class) {
        node->labels.push_back("class");
    }
    skip_attributes();
    if (peek().kind == TokenKind::Identifier) {
        node->text = peek().text;
        if (is_class) {
            typedef_names_.insert(node->text);
            class_names_.insert(node->text);
        } else if (cxx_mode_ && !specifiers_have_typedef_) {
            typedef_names_.insert(node->text);
            class_names_.insert(node->text);
        }
        ++position_;
    }
    skip_attributes();
    if (match(":")) {
        if (peek().kind == TokenKind::Identifier &&
            (peek().text == "public" || peek().text == "private" ||
             peek().text == "protected")) {
            ++position_;
        }
        if (peek().kind == TokenKind::Identifier) {
            node->labels.push_back("base:" + peek().text);
            ++position_;
        }
    }
    if (match("{")) {
        node->labels.push_back("definition");
        const auto previous_class = current_class_name_;
        current_class_name_ = node->text;
        while (!check("}") && !at_end()) {
            if (peek().kind == TokenKind::Identifier &&
                (peek().text == "public" || peek().text == "private" ||
                 peek().text == "protected") &&
                peek(1).text == ":") {
                position_ += 2;
                continue;
            }
            auto member = parse_member_declaration();
            if (member) {
                node->children.push_back(std::move(member));
            } else {
                synchronize();
                if (!at_end() && !check("}")) {
                    ++position_;
                }
            }
        }
        expect("}", "after struct or union body");
        current_class_name_ = previous_class;
        if (!node->text.empty() && !specifiers_have_typedef_ && cxx_mode_) {
            typedef_names_.insert(node->text);
        }
    }
    return node;
}

std::unique_ptr<AstNode> Parser::parse_member_declaration() {
    const auto location = peek().location;
    if (peek().text == "explicit") {
        ++position_;
    }
    bool is_virtual = false;
    if (peek().kind == TokenKind::Identifier && peek().text == "virtual") {
        is_virtual = true;
        ++position_;
    }
    if (check("~")) {
        ++position_;
        std::string destructor_name = "~";
        if (peek().kind == TokenKind::Identifier) {
            destructor_name += peek().text;
            ++position_;
        }
        if (match("(")) {
            std::size_t depth = 1;
            while (!at_end() && depth > 0) {
                if (check("(")) {
                    ++depth;
                } else if (check(")")) {
                    --depth;
                }
                ++position_;
            }
        }
        bool overrides = false;
        while (check("override") || check("final") || check("noexcept")) {
            const bool is_noexcept = check("noexcept");
            overrides = overrides || check("override");
            ++position_;
            if (is_noexcept && match("(")) {
                std::size_t depth = 1;
                while (!at_end() && depth > 0) {
                    if (check("(")) {
                        ++depth;
                    } else if (check(")")) {
                        --depth;
                    }
                    ++position_;
                }
            }
        }
        auto specifiers = make_ast(AstKind::DeclSpecifiers, location);
        specifiers->labels.push_back("void");
        if (is_virtual || overrides) {
            specifiers->labels.push_back("virtual");
        }
        auto declarator = make_ast(AstKind::FunctionDeclarator, location);
        declarator->labels.push_back("prototype");
        declarator->children.push_back(
            make_ast(AstKind::Declarator, location, destructor_name));
        if (check("{")) {
            auto definition = make_ast(AstKind::FunctionDefinition, location,
                                       destructor_name);
            definition->children.push_back(std::move(specifiers));
            definition->children.push_back(std::move(declarator));
            definition->children.push_back(parse_compound_statement());
            return definition;
        }
        expect(";", "after destructor declaration");
        auto member = make_ast(AstKind::MemberDeclaration, location);
        member->children.push_back(std::move(specifiers));
        member->children.push_back(std::move(declarator));
        return member;
    }
    if (!current_class_name_.empty() && peek().kind == TokenKind::Identifier &&
        peek().text == current_class_name_ && peek(1).text == "(") {
        auto specifiers = make_ast(AstKind::DeclSpecifiers, peek().location);
        specifiers->labels.push_back("void");
        auto declarator = parse_declarator(false);
        if (!declarator) {
            return nullptr;
        }
        if (check("{") || check(":")) {
            std::unique_ptr<AstNode> member_initializers;
            if (match(":")) {
                member_initializers =
                    make_ast(AstKind::MemberInitializerList, peek().location);
                while (!check("{") && !at_end()) {
                    if (peek().kind != TokenKind::Identifier) {
                        error(peek(), "expected member initializer");
                        break;
                    }
                    auto initializer = make_ast(AstKind::MemberInitializer,
                                                peek().location, peek().text);
                    ++position_;
                    if (match("(")) {
                        initializer->children.push_back(parse_argument_list());
                    }
                    member_initializers->children.push_back(std::move(initializer));
                    if (!match(",")) {
                        break;
                    }
                }
            }
            auto definition = make_ast(AstKind::FunctionDefinition, declarator->location,
                                       declarator_name(*declarator));
            definition->children.push_back(std::move(specifiers));
            definition->children.push_back(std::move(declarator));
            definition->children.push_back(parse_compound_statement());
            if (member_initializers) {
                definition->children.push_back(std::move(member_initializers));
            }
            return definition;
        }
        return nullptr;
    }
    auto specifiers = parse_decl_specifiers();
    if (!specifiers) {
        return nullptr;
    }
    if (is_virtual) {
        specifiers->labels.push_back("virtual");
    }

    auto member = make_ast(AstKind::MemberDeclaration, specifiers->location);
    member->children.push_back(std::move(specifiers));
    if (match(";")) {
        return member;
    }

    for (;;) {
        auto declarator = parse_declarator(false);
        if (!declarator) {
            break;
        }
        while (peek().text == "const" || peek().text == "volatile" ||
               peek().text == "override" || peek().text == "noexcept" ||
               peek().text == "final") {
            ++position_;
        }
        if (contains_function_declarator(*declarator) &&
            (check("{") || check(":"))) {
            std::unique_ptr<AstNode> member_initializers;
            if (match(":")) {
                member_initializers =
                    make_ast(AstKind::MemberInitializerList, peek().location);
                while (!check("{") && !at_end()) {
                    if (peek().kind != TokenKind::Identifier) {
                        error(peek(), "expected member initializer");
                        break;
                    }
                    auto initializer = make_ast(AstKind::MemberInitializer,
                                                peek().location, peek().text);
                    ++position_;
                    if (match("(")) {
                        initializer->children.push_back(parse_argument_list());
                    }
                    member_initializers->children.push_back(std::move(initializer));
                    if (!match(",")) {
                        break;
                    }
                }
            }
            auto definition = make_ast(AstKind::FunctionDefinition, declarator->location,
                                       declarator_name(*declarator));
            definition->children.push_back(std::move(member->children[0]));
            definition->children.push_back(std::move(declarator));
            definition->children.push_back(parse_compound_statement());
            if (member_initializers) {
                definition->children.push_back(std::move(member_initializers));
            }
            return definition;
        }
        if (match(":")) {
            auto width = parse_assignment_expression();
            if (width) {
                member->children.push_back(std::move(width));
            }
        }
        const bool field_is_function =
            contains_function_declarator(*declarator);
        member->children.push_back(std::move(declarator));
        auto field_name = declarator_name(*member->children.back());
        skip_attributes();
        while (peek().text == "const" || peek().text == "volatile" ||
               peek().text == "override" || peek().text == "noexcept" ||
               peek().text == "final") {
            ++position_;
        }
        if (!field_is_function && !field_name.empty() && match("=")) {
            auto initializer =
                make_ast(AstKind::DefaultMemberInitializer, peek().location,
                         field_name);
            if (auto value = parse_assignment_expression()) {
                initializer->children.push_back(std::move(value));
            }
            member->children.push_back(std::move(initializer));
        } else if (!field_is_function && !field_name.empty() && match("{")) {
            auto initializer =
                make_ast(AstKind::DefaultMemberInitializer, peek().location,
                         field_name);
            auto list = make_ast(AstKind::InitializerList, peek().location);
            while (!check("}") && !at_end()) {
                auto value = parse_assignment_expression();
                if (value) {
                    list->children.push_back(std::move(value));
                }
                if (!match(",")) {
                    break;
                }
            }
            expect("}", "after member initializer");
            initializer->children.push_back(std::move(list));
            member->children.push_back(std::move(initializer));
        }
        if (!match(",")) {
            break;
        }
    }
    if (match("=")) {
        static_cast<void>(parse_assignment_expression());
        member->labels.push_back("pure");
    }
    expect(";", "after member declaration");
    return member;
}

std::unique_ptr<AstNode> Parser::parse_enum_specifier() {
    const auto location = peek().location;
    ++position_;  // enum
    auto node = make_ast(AstKind::EnumSpecifier, location);
    bool scoped = false;
    if (check("class") || check("struct")) {
        scoped = true;
        node->labels.push_back("scoped");
        ++position_;
    }
    if (peek().kind == TokenKind::Identifier) {
        node->text = peek().text;
        if (cxx_mode_ && !specifiers_have_typedef_) {
            typedef_names_.insert(node->text);
        }
        ++position_;
    }
    skip_attributes();
    if (match(":")) {
        static_cast<void>(parse_type_name());
    }
    if (match("{")) {
        while (!check("}") && !at_end()) {
            if (peek().kind != TokenKind::Identifier) {
                error(peek(), "expected enumerator name");
                break;
            }
            auto enumerator = make_ast(
                AstKind::Enumerator, peek().location,
                scoped && !node->text.empty() ? node->text + "::" + peek().text
                                              : peek().text);
            ++position_;
            if (match("=")) {
                auto value = parse_assignment_expression();
                if (value) {
                    enumerator->children.push_back(std::move(value));
                }
            }
            node->children.push_back(std::move(enumerator));
            if (!match(",")) {
                break;
            }
        }
        expect("}", "after enum body");
    }
    return node;
}

std::unique_ptr<AstNode> Parser::parse_initializer() {
    if (!match("{")) {
        return parse_assignment_expression();
    }

    auto list = make_ast(AstKind::InitializerList, peek().location);
    while (!check("}") && !at_end()) {
        std::unique_ptr<AstNode> designator;
        if (match(".")) {
            designator = make_ast(AstKind::DesignatedInitializer, peek().location);
            do {
                if (peek().kind != TokenKind::Identifier) {
                    error(peek(), "expected member designator");
                    break;
                }
                designator->children.push_back(
                    make_ast(AstKind::Identifier, peek().location, peek().text));
                ++position_;
            } while (match("."));
            expect("=", "after member designator");
        } else if (match("[")) {
            auto index = parse_assignment_expression();
            expect("]", "after array designator");
            expect("=", "after array designator");
            designator = make_ast(AstKind::DesignatedInitializer, index ? index->location
                                                                         : peek().location);
            if (index) {
                designator->children.push_back(std::move(index));
            }
        }

        auto value = parse_initializer();
        if (designator) {
            if (value) {
                designator->children.push_back(std::move(value));
            }
            list->children.push_back(std::move(designator));
        } else if (value) {
            list->children.push_back(std::move(value));
        }
        if (!match(",")) {
            break;
        }
    }
    expect("}", "after initializer list");
    return list;
}

std::unique_ptr<AstNode> Parser::parse_compound_statement() {
    const auto location = peek().location;
    expect("{", "to start compound statement");
    auto body = make_ast(AstKind::CompoundStatement, location);
    while (!check("}") && !at_end()) {
        std::unique_ptr<AstNode> item;
        if ((check("_Static_assert") || (cxx_mode_ && check("static_assert")))) {
            item = parse_static_assert();
        } else if (is_declaration_start()) {
            item = parse_declaration();
        } else {
            item = parse_statement();
        }
        if (item) {
            body->children.push_back(std::move(item));
        } else {
            synchronize();
            if (!at_end() && !check("}")) {
                ++position_;
            }
        }
    }
    expect("}", "to close compound statement");
    return body;
}

bool Parser::is_declaration_start() const {
    const Token& token = peek();
    if (token.text == "__extension__") {
        const Token& next = peek(1);
        return (next.kind == TokenKind::Keyword && is_decl_specifier_keyword(next.text)) ||
               (next.kind == TokenKind::Identifier && is_typedef_name(next.text));
    }
    if (token.kind == TokenKind::Keyword && is_decl_specifier_keyword(token.text)) {
        return true;
    }
    if (token.kind == TokenKind::Keyword && (token.text == "typeof" || token.text == "__typeof__")) {
        return true;
    }
    if (cxx_mode_ && token.kind == TokenKind::Keyword &&
        (token.text == "namespace" || token.text == "using")) {
        return true;
    }
    if (cxx_mode_ && token.kind == TokenKind::Identifier &&
        peek(1).is("::") && peek(2).kind == TokenKind::Identifier &&
        is_typedef_name(peek(2).text) && peek(2).text != token.text) {
        return true;
    }
    if (cxx_mode_ && token.kind == TokenKind::Identifier && peek(1).is("::")) {
        return false;
    }
    if (token.kind == TokenKind::Identifier && is_typedef_name(token.text)) {
        return true;
    }
    if (cxx_mode_ && token.kind == TokenKind::Identifier && token.text == "decltype" &&
        peek(1).is("(")) {
        return true;
    }
    if (cxx_mode_ && token.kind == TokenKind::Identifier && token.text == "alignas" &&
        peek(1).is("(")) {
        return true;
    }
    return token.text == "_Static_assert" ||
           (cxx_mode_ && token.text == "static_assert") ||
           token.text == "__attribute__" || token.text == "__declspec";
}

bool Parser::is_type_name_start() const {
    const Token& token = peek();
    if (token.kind == TokenKind::Keyword &&
        (is_type_specifier(token.text) || is_qualifier(token.text) || token.text == "_Alignas")) {
        return true;
    }
    return token.kind == TokenKind::Identifier && is_typedef_name(token.text);
}

std::unique_ptr<AstNode> Parser::parse_statement() {
    if (check("{")) {
        return parse_compound_statement();
    }
    if (match(";")) {
        return make_ast(AstKind::NullStatement, peek().location);
    }
    if (check("if")) {
        const auto location = peek().location;
        ++position_;
        const bool is_constexpr = match("constexpr");
        expect("(", "after 'if'");
        auto condition = parse_expression();
        expect(")", "after 'if' condition");
        auto statement = parse_statement();
        auto node = make_ast(AstKind::IfStatement, location);
        if (is_constexpr) {
            node->labels.push_back("constexpr");
        }
        node->children.push_back(std::move(condition));
        node->children.push_back(std::move(statement));
        if (match("else")) {
            node->children.push_back(parse_statement());
        }
        return node;
    }
    if (check("while")) {
        const auto location = peek().location;
        ++position_;
        expect("(", "after 'while'");
        auto condition = parse_expression();
        expect(")", "after 'while' condition");
        auto node = make_ast(AstKind::WhileStatement, location);
        node->children.push_back(std::move(condition));
        node->children.push_back(parse_statement());
        return node;
    }
    if (check("do")) {
        const auto location = peek().location;
        ++position_;
        auto body = parse_statement();
        expect("while", "after 'do' body");
        expect("(", "after 'while'");
        auto condition = parse_expression();
        expect(")", "after 'do-while' condition");
        expect(";", "after 'do-while'");
        auto node = make_ast(AstKind::DoStatement, location);
        node->children.push_back(std::move(body));
        node->children.push_back(std::move(condition));
        return node;
    }
    if (check("for")) {
        const auto location = peek().location;
        ++position_;
        expect("(", "after 'for'");
        if (cxx_mode_ && is_declaration_start()) {
            const auto saved = position_;
            auto specifiers = parse_decl_specifiers();
            auto declarator = parse_declarator(false);
            if (specifiers && declarator && match(":")) {
                auto range = parse_expression();
                expect(")", "after range-for");
                auto node = make_ast(AstKind::RangeForStatement, location);
                node->children.push_back(std::move(specifiers));
                node->children.push_back(std::move(declarator));
                node->children.push_back(std::move(range));
                node->children.push_back(parse_statement());
                return node;
            }
            position_ = saved;
        }
        auto node = make_ast(AstKind::ForStatement, location);

        if (check(";")) {
            ++position_;
            node->children.push_back(nullptr);
        } else if (is_declaration_start()) {
            node->children.push_back(parse_declaration());
        } else {
            node->children.push_back(parse_expression());
            expect(";", "after 'for' initializer");
        }
        if (!check(";")) {
            node->children.push_back(parse_expression());
        } else {
            node->children.push_back(nullptr);
        }
        expect(";", "after 'for' condition");
        if (!check(")")) {
            node->children.push_back(parse_expression());
        } else {
            node->children.push_back(nullptr);
        }
        expect(")", "after 'for' clauses");
        node->children.push_back(parse_statement());
        return node;
    }
    if (check("switch")) {
        const auto location = peek().location;
        ++position_;
        expect("(", "after 'switch'");
        auto condition = parse_expression();
        expect(")", "after 'switch' condition");
        auto node = make_ast(AstKind::SwitchStatement, location);
        node->children.push_back(std::move(condition));
        node->children.push_back(parse_statement());
        return node;
    }
    if (check("case")) {
        const auto location = peek().location;
        ++position_;
        auto value = parse_conditional_expression();
        expect(":", "after 'case' value");
        auto node = make_ast(AstKind::CaseStatement, location);
        node->children.push_back(std::move(value));
        if (!check("}") && !at_end()) {
            node->children.push_back(parse_statement());
        }
        return node;
    }
    if (check("default")) {
        const auto location = peek().location;
        ++position_;
        expect(":", "after 'default'");
        auto node = make_ast(AstKind::DefaultStatement, location);
        if (!check("}") && !at_end()) {
            node->children.push_back(parse_statement());
        }
        return node;
    }
    if (check("break")) {
        const auto location = peek().location;
        ++position_;
        expect(";", "after 'break'");
        return make_ast(AstKind::BreakStatement, location);
    }
    if (check("continue")) {
        const auto location = peek().location;
        ++position_;
        expect(";", "after 'continue'");
        return make_ast(AstKind::ContinueStatement, location);
    }
    if (check("return")) {
        const auto location = peek().location;
        ++position_;
        auto node = make_ast(AstKind::ReturnStatement, location);
        if (!check(";")) {
            node->children.push_back(parse_expression());
        }
        expect(";", "after 'return'");
        return node;
    }
    if (check("goto")) {
        const auto location = peek().location;
        ++position_;
        if (peek().kind != TokenKind::Identifier) {
            error(peek(), "expected label after 'goto'");
            return nullptr;
        }
        auto node = make_ast(AstKind::GotoStatement, location, peek().text);
        ++position_;
        expect(";", "after 'goto'");
        return node;
    }
    if (peek().kind == TokenKind::Identifier && peek(1).text == ":") {
        const auto location = peek().location;
        const auto label = peek().text;
        position_ += 2;
        auto node = make_ast(AstKind::LabelStatement, location, label);
        if (!check("}") && !at_end()) {
            node->children.push_back(parse_statement());
        }
        return node;
    }
    if ((check("_Static_assert") || (cxx_mode_ && check("static_assert")))) {
        return parse_static_assert();
    }

    const auto location = peek().location;
    auto expression = parse_expression();
    expect(";", "after expression statement");
    auto node = make_ast(AstKind::ExpressionStatement, location);
    if (expression) {
        node->children.push_back(std::move(expression));
    }
    return node;
}

std::unique_ptr<AstNode> Parser::parse_static_assert() {
    const auto location = peek().location;
    ++position_;
    expect("(", "after '_Static_assert'");
    auto condition = parse_assignment_expression();
    std::unique_ptr<AstNode> message;
    if (match(",")) {
        message = parse_primary_expression();
    }
    expect(")", "after '_Static_assert'");
    expect(";", "after '_Static_assert'");
    auto node = make_ast(AstKind::StaticAssertDeclaration, location);
    if (condition) {
        node->children.push_back(std::move(condition));
    }
    if (message) {
        node->children.push_back(std::move(message));
    }
    return node;
}

std::unique_ptr<AstNode> Parser::parse_expression() {
    auto expression = parse_assignment_expression();
    if (!expression) {
        return nullptr;
    }
    while (match(",")) {
        auto right = parse_assignment_expression();
        if (!right) {
            break;
        }
        auto comma = make_ast(AstKind::CommaExpression, expression->location);
        comma->children.push_back(std::move(expression));
        comma->children.push_back(std::move(right));
        expression = std::move(comma);
    }
    return expression;
}

std::unique_ptr<AstNode> Parser::parse_assignment_expression() {
    auto left = parse_conditional_expression();
    if (is_assignment_operator(peek().text)) {
        const auto location = peek().location;
        const auto operation = peek().text;
        ++position_;
        auto right = parse_assignment_expression();
        auto node = make_ast(AstKind::AssignmentExpression, location, operation);
        node->children.push_back(std::move(left));
        node->children.push_back(std::move(right));
        return node;
    }
    return left;
}

std::unique_ptr<AstNode> Parser::parse_conditional_expression() {
    auto condition = parse_binary_expression(1);
    if (!condition) {
        return nullptr;
    }
    if (match("?")) {
        auto when_true = parse_expression();
        expect(":", "in conditional expression");
        auto when_false = parse_conditional_expression();
        auto node = make_ast(AstKind::ConditionalExpression, condition->location);
        node->children.push_back(std::move(condition));
        node->children.push_back(std::move(when_true));
        node->children.push_back(std::move(when_false));
        return node;
    }
    return condition;
}

bool Parser::is_assignment_operator(std::string_view spelling) const {
    return spelling == "=" || spelling == "+=" || spelling == "-=" || spelling == "*=" ||
           spelling == "/=" || spelling == "%=" || spelling == "<<=" || spelling == ">>=" ||
           spelling == "&=" || spelling == "^=" || spelling == "|=";
}

int Parser::precedence(std::string_view spelling) const {
    if (spelling == "||") {
        return 1;
    }
    if (spelling == "&&") {
        return 2;
    }
    if (spelling == "|") {
        return 3;
    }
    if (spelling == "^") {
        return 4;
    }
    if (spelling == "&") {
        return 5;
    }
    if (spelling == "==" || spelling == "!=") {
        return 6;
    }
    if (spelling == "<" || spelling == ">" || spelling == "<=" || spelling == ">=") {
        return 7;
    }
    if (spelling == "<<" || spelling == ">>") {
        return 8;
    }
    if (spelling == "+" || spelling == "-") {
        return 9;
    }
    if (spelling == "*" || spelling == "/" || spelling == "%") {
        return 10;
    }
    return 0;
}

std::unique_ptr<AstNode> Parser::parse_binary_expression(int minimum_precedence) {
    auto left = parse_unary_expression();
    while (!at_end()) {
        const auto operation_precedence = precedence(peek().text);
        if (operation_precedence < minimum_precedence || operation_precedence == 0) {
            break;
        }
        const auto location = peek().location;
        const auto operation = peek().text;
        ++position_;
        auto right = parse_binary_expression(operation_precedence + 1);
        auto node = make_ast(AstKind::BinaryExpression, location, operation);
        node->children.push_back(std::move(left));
        node->children.push_back(std::move(right));
        left = std::move(node);
    }
    return left;
}

std::unique_ptr<AstNode> Parser::parse_unary_expression() {
    const auto location = peek().location;
    if (cxx_mode_ && peek().kind == TokenKind::Identifier &&
        (peek().text == "static_cast" || peek().text == "reinterpret_cast" ||
         peek().text == "const_cast")) {
        ++position_;
        expect("<", "after cast keyword");
        auto type = parse_type_name();
        expect(">", "after cast type");
        expect("(", "after cast type");
        auto operand = parse_expression();
        expect(")", "after cast operand");
        auto node = make_ast(AstKind::CastExpression, location);
        if (type) {
            node->children.push_back(std::move(type));
        }
        if (operand) {
            node->children.push_back(std::move(operand));
        }
        return node;
    }
    if (cxx_mode_ && check("[")) {
        return parse_lambda_expression();
    }
    if (check("new")) {
        ++position_;
        if (check("(")) {
            ++position_;
            auto placement = make_ast(AstKind::UnaryExpression, location,
                                      "placement_new");
            auto address = parse_expression();
            if (address) {
                placement->children.push_back(std::move(address));
            }
            expect(")", "after placement address");
            if (is_type_name_start()) {
                placement->children.push_back(parse_type_name());
            } else if (peek().kind == TokenKind::Identifier) {
                placement->children.push_back(
                    make_ast(AstKind::TypeName, peek().location, peek().text));
                ++position_;
            }
            if (match("(")) {
                placement->children.push_back(parse_argument_list());
            }
            return placement;
        }
        auto node = make_ast(AstKind::UnaryExpression, location, "new");
        if (peek().kind == TokenKind::Identifier && peek(1).text == "(") {
            node->children.push_back(
                make_ast(AstKind::TypeName, peek().location, peek().text));
            ++position_;
        } else if (is_type_name_start()) {
            auto type = parse_type_name();
            if (type) {
                node->children.push_back(std::move(type));
            }
        } else if (peek().kind == TokenKind::Identifier) {
            auto type = make_ast(AstKind::TypeName, peek().location, peek().text);
            ++position_;
            node->children.push_back(std::move(type));
        }
        if (match("(")) {
            node->children.push_back(parse_argument_list());
        } else if (match("[")) {
            auto count = parse_assignment_expression();
            if (count) {
                node->children.push_back(std::move(count));
            }
            expect("]", "after new array bound");
        }
        return node;
    }
    if (check("delete")) {
        ++position_;
        bool array = false;
        if (match("[")) {
            expect("]", "after delete[]");
            array = true;
        }
        auto operand = parse_unary_expression();
        auto node = make_ast(AstKind::UnaryExpression, location,
                             array ? "delete[]" : "delete");
        node->children.push_back(std::move(operand));
        return node;
    }
    if (check("__extension__")) {
        ++position_;
        return parse_unary_expression();
    }
    if (check("++") || check("--") || check("+") || check("-") || check("!") || check("~") ||
        check("*") || check("&")) {
        const auto operation = peek().text;
        ++position_;
        auto operand = parse_unary_expression();
        auto node = make_ast(AstKind::UnaryExpression, location, operation);
        node->children.push_back(std::move(operand));
        return node;
    }
    if (check("sizeof") || check("_Alignof")) {
        const bool is_sizeof = check("sizeof");
        ++position_;
        auto node = make_ast(is_sizeof ? AstKind::SizeofExpression : AstKind::AlignofExpression,
                             location);
        if (check("(") && peek(1).kind != TokenKind::Eof &&
            (is_type_name_start() || peek(1).kind == TokenKind::Keyword ||
             peek(1).kind == TokenKind::Identifier)) {
            const auto saved = position_;
            ++position_;
            if (is_type_name_start()) {
                auto type = parse_type_name();
                expect(")", "after type in sizeof");
                if (type) {
                    node->children.push_back(std::move(type));
                }
                return node;
            }
            position_ = saved;
        }
        node->children.push_back(parse_unary_expression());
        return node;
    }

    if (check("(")) {
        const auto saved = position_;
        ++position_;
        if (is_type_name_start()) {
            auto type = parse_type_name();
            if (type && match(")")) {
                if (check("{")) {
                    auto initializer = parse_initializer();
                    auto node = make_ast(AstKind::CompoundLiteral, location);
                    node->children.push_back(std::move(type));
                    if (initializer) {
                        node->children.push_back(std::move(initializer));
                    }
                    return node;
                }
                auto operand = parse_unary_expression();
                auto node = make_ast(AstKind::CastExpression, location);
                node->children.push_back(std::move(type));
                node->children.push_back(std::move(operand));
                return node;
            }
        }
        position_ = saved;
    }

    return parse_postfix_expression();
}

std::unique_ptr<AstNode> Parser::parse_lambda_expression() {
    const auto location = peek().location;
    expect("[", "at lambda start");
    auto node = make_ast(AstKind::LambdaExpression, location);
    std::vector<std::unique_ptr<AstNode>> init_captures;
    while (!check("]") && !at_end()) {
        if ((check("=") || check("&")) && peek(1).is("]")) {
            node->labels.push_back(peek().text);
            ++position_;
            break;
        }
        const bool by_reference = match("&");
        if (peek().kind != TokenKind::Identifier) {
            error(peek(), "expected lambda capture name");
            break;
        }
        const auto capture_name = peek().text;
        ++position_;
        if (match("=")) {
            auto initializer = parse_assignment_expression();
            auto capture = make_ast(AstKind::LambdaCapture, location, capture_name);
            if (initializer) {
                capture->children.push_back(std::move(initializer));
            }
            init_captures.push_back(std::move(capture));
        } else {
            node->labels.push_back((by_reference ? "&" : "") + capture_name);
        }
        if (!match(",")) {
            break;
        }
    }
    expect("]", "after lambda capture list");
    auto declarator = make_ast(AstKind::FunctionDeclarator, location);
    declarator->labels.push_back("prototype");
    declarator->children.push_back(make_ast(AstKind::Declarator, location));
    if (match("(")) {
        if (!check(")")) {
            for (;;) {
                auto parameter = make_ast(AstKind::ParameterDeclaration, peek().location);
                auto specifiers = parse_decl_specifiers();
                if (!specifiers) {
                    error(peek(), "expected lambda parameter");
                    break;
                }
                parameter->children.push_back(std::move(specifiers));
                if (!check(",") && !check(")")) {
                    auto parameter_declarator = parse_declarator(true);
                    if (parameter_declarator) {
                        parameter->children.push_back(std::move(parameter_declarator));
                    }
                }
                declarator->children.push_back(std::move(parameter));
                if (match(",")) {
                    continue;
                }
                break;
            }
        }
        expect(")", "after lambda parameter list");
    }
    if (check("mutable")) {
        ++position_;
    }
    node->children.push_back(std::move(declarator));
    node->children.push_back(parse_compound_statement());
    for (auto& capture : init_captures) {
        node->children.push_back(std::move(capture));
    }
    return node;
}

std::unique_ptr<AstNode> Parser::parse_postfix_expression() {
    auto expression = parse_primary_expression();
    if (!expression) {
        return nullptr;
    }
    for (;;) {
        if (cxx_mode_ && check("<") &&
            expression->kind == AstKind::Identifier) {
            const auto saved = position_;
            ++position_;
            int depth = 1;
            while (!at_end() && depth > 0) {
                if (check("<")) {
                    ++depth;
                } else if (check(">")) {
                    --depth;
                }
                ++position_;
            }
            if (depth == 0 && check("(")) {
                continue;
            }
            position_ = saved;
        }
        if (match("[")) {
            auto index = parse_expression();
            expect("]", "after subscript");
            auto node = make_ast(AstKind::SubscriptExpression, expression->location);
            node->children.push_back(std::move(expression));
            node->children.push_back(std::move(index));
            expression = std::move(node);
            continue;
        }
        if (match("(")) {
            auto arguments = parse_argument_list();
            auto node = make_ast(AstKind::CallExpression, expression->location);
            node->children.push_back(std::move(expression));
            node->children.push_back(std::move(arguments));
            expression = std::move(node);
            continue;
        }
        if (check(".") || check("->")) {
            const auto operation = peek().text;
            const auto location = peek().location;
            ++position_;
            if (peek().kind != TokenKind::Identifier) {
                error(peek(), "expected member name");
                break;
            }
            auto node = make_ast(AstKind::MemberExpression, location, peek().text);
            node->labels.push_back(operation);
            node->children.push_back(std::move(expression));
            ++position_;
            expression = std::move(node);
            continue;
        }
        if (check("++") || check("--")) {
            const auto operation = peek().text;
            const auto location = peek().location;
            ++position_;
            auto node = make_ast(AstKind::PostfixExpression, location, operation);
            node->children.push_back(std::move(expression));
            expression = std::move(node);
            continue;
        }
        break;
    }
    return expression;
}

std::unique_ptr<AstNode> Parser::parse_argument_list() {
    auto node = make_ast(AstKind::ExpressionStatement, peek().location);
    if (check(")")) {
        ++position_;
        return node;
    }
    for (;;) {
        auto argument = parse_assignment_expression();
        if (argument) {
            node->children.push_back(std::move(argument));
        }
        if (!match(",")) {
            break;
        }
    }
    expect(")", "after argument list");
    return node;
}

std::unique_ptr<AstNode> Parser::parse_primary_expression() {
    const Token token = peek();
    if (cxx_mode_ && token.is("{")) {
        return parse_initializer();
    }
    switch (token.kind) {
    case TokenKind::Integer:
        ++position_;
        return make_ast(AstKind::IntegerLiteral, token.location, token.text);
    case TokenKind::Float:
        ++position_;
        return make_ast(AstKind::FloatLiteral, token.location, token.text);
    case TokenKind::Char:
        ++position_;
        return make_ast(AstKind::CharLiteral, token.location, token.text);
    case TokenKind::String: {
        std::string prefix;
        std::string contents;
        bool first = true;
        while (peek().kind == TokenKind::String) {
            const auto raw = peek().text;
            const auto quote = raw.find('"');
            if (quote != std::string::npos) {
                if (first) {
                    prefix = raw.substr(0, quote);
                    first = false;
                }
                auto body = raw.substr(quote + 1);
                if (!body.empty() && body.back() == '"') {
                    body.pop_back();
                }
                contents += body;
            }
            ++position_;
        }
        return make_ast(AstKind::StringLiteral, token.location,
                        prefix + "\"" + contents + "\"");
    }
    case TokenKind::Identifier:
        if (cxx_mode_ && token.text == "nullptr") {
            ++position_;
            return make_ast(AstKind::IntegerLiteral, token.location, "0");
        }
        if (cxx_mode_ && (token.text == "true" || token.text == "false")) {
            ++position_;
            return make_ast(AstKind::IntegerLiteral, token.location,
                            token.text == "true" ? "1" : "0");
        }
        if (token.text == "_Generic") {
            return parse_generic_selection();
        }
        if (token.text == "__builtin_offsetof" && peek(1).is("(")) {
            return parse_offsetof_expression();
        }
        ++position_;
        if (cxx_mode_ && check("::") && peek(1).kind == TokenKind::Identifier) {
            std::string qualified = token.text;
            while (check("::") && peek(1).kind == TokenKind::Identifier) {
                ++position_;
                qualified += "::";
                qualified += peek().text;
                ++position_;
            }
            return make_ast(AstKind::Identifier, token.location, std::move(qualified));
        }
        return make_ast(AstKind::Identifier, token.location, token.text);
    case TokenKind::Keyword:
        if (token.text == "_Generic") {
            return parse_generic_selection();
        }
        break;
    default:
        break;
    }

    if (match("(")) {
        auto expression = parse_expression();
        expect(")", "after parenthesized expression");
        return expression;
    }

    error(token, "expected expression");
    if (!at_end()) {
        ++position_;
    }
    return nullptr;
}

std::unique_ptr<AstNode> Parser::parse_offsetof_expression() {
    const auto location = peek().location;
    ++position_;  // __builtin_offsetof
    expect("(", "after '__builtin_offsetof'");
    auto node = make_ast(AstKind::OffsetofExpression, location);
    auto type = parse_type_name();
    if (type) {
        node->children.push_back(std::move(type));
    }
    expect(",", "in '__builtin_offsetof'");
    for (;;) {
        if (peek().kind != TokenKind::Identifier) {
            error(peek(), "expected member designator in '__builtin_offsetof'");
            break;
        }
        node->children.push_back(make_ast(AstKind::Identifier, peek().location, peek().text));
        ++position_;
        if (!match(".")) {
            break;
        }
    }
    expect(")", "after '__builtin_offsetof'");
    return node;
}

std::unique_ptr<AstNode> Parser::parse_generic_selection() {
    const auto location = peek().location;
    ++position_;
    expect("(", "after '_Generic'");
    auto node = make_ast(AstKind::GenericSelection, location);
    node->children.push_back(parse_assignment_expression());
    while (match(",")) {
        if (match("default")) {
            node->labels.emplace_back("default");
        } else {
            node->children.push_back(parse_type_name());
        }
        expect(":", "in '_Generic' association");
        node->children.push_back(parse_assignment_expression());
    }
    expect(")", "after '_Generic'");
    return node;
}

std::unique_ptr<AstNode> Parser::parse_type_name() {
    auto specifiers = parse_decl_specifiers();
    if (!specifiers) {
        error(peek(), "expected type name");
        return nullptr;
    }
    auto declarator = parse_declarator(true);
    auto node = make_ast(AstKind::TypeName, specifiers->location);
    node->children.push_back(std::move(specifiers));
    if (declarator) {
        node->children.push_back(std::move(declarator));
    }
    return node;
}

std::string Parser::declarator_name(const AstNode& declarator) const {
    if (declarator.kind == AstKind::Declarator) {
        return declarator.text;
    }
    if (declarator.kind == AstKind::PointerDeclarator ||
        declarator.kind == AstKind::ReferenceDeclarator ||
        declarator.kind == AstKind::ArrayDeclarator ||
        declarator.kind == AstKind::FunctionDeclarator) {
        for (const auto& child : declarator.children) {
            if (child) {
                const auto name = declarator_name(*child);
                if (!name.empty()) {
                    return name;
                }
            }
        }
    }
    return {};
}

void Parser::register_typedefs(const AstNode& specifiers, const AstNode& declaration) {
    bool is_typedef = false;
    for (const auto& label : specifiers.labels) {
        if (label == "typedef") {
            is_typedef = true;
            break;
        }
    }
    if (!is_typedef) {
        return;
    }
    for (const auto& child : declaration.children) {
        if (!child || child->kind != AstKind::InitDeclarator || child->children.empty()) {
            continue;
        }
        const auto name = declarator_name(*child->children.front());
        if (!name.empty()) {
            typedef_names_.insert(name);
        }
    }
}

}  // namespace krc::frontend
