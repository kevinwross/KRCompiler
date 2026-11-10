#include "krc/frontend/ast.h"

#include <string_view>
#include <utility>

namespace krc::frontend {

std::string_view ast_kind_name(AstKind kind) noexcept {
    switch (kind) {
    case AstKind::TranslationUnit:
        return "TranslationUnit";
    case AstKind::Declaration:
        return "Declaration";
    case AstKind::DeclSpecifiers:
        return "DeclSpecifiers";
    case AstKind::AlignasSpecifier:
        return "AlignasSpecifier";
    case AstKind::InitDeclarator:
        return "InitDeclarator";
    case AstKind::FunctionDefinition:
        return "FunctionDefinition";
    case AstKind::Declarator:
        return "Declarator";
    case AstKind::PointerDeclarator:
        return "PointerDeclarator";
    case AstKind::ReferenceDeclarator:
        return "ReferenceDeclarator";
    case AstKind::ArrayDeclarator:
        return "ArrayDeclarator";
    case AstKind::FunctionDeclarator:
        return "FunctionDeclarator";
    case AstKind::ParameterDeclaration:
        return "ParameterDeclaration";
    case AstKind::MemberDeclaration:
        return "MemberDeclaration";
    case AstKind::StructSpecifier:
        return "StructSpecifier";
    case AstKind::UnionSpecifier:
        return "UnionSpecifier";
    case AstKind::EnumSpecifier:
        return "EnumSpecifier";
    case AstKind::Enumerator:
        return "Enumerator";
    case AstKind::TypeName:
        return "TypeName";
    case AstKind::InitializerList:
        return "InitializerList";
    case AstKind::DesignatedInitializer:
        return "DesignatedInitializer";
    case AstKind::CompoundStatement:
        return "CompoundStatement";
    case AstKind::DeclarationStatement:
        return "DeclarationStatement";
    case AstKind::ExpressionStatement:
        return "ExpressionStatement";
    case AstKind::IfStatement:
        return "IfStatement";
    case AstKind::WhileStatement:
        return "WhileStatement";
    case AstKind::DoStatement:
        return "DoStatement";
    case AstKind::ForStatement:
        return "ForStatement";
    case AstKind::RangeForStatement:
        return "RangeForStatement";
    case AstKind::SwitchStatement:
        return "SwitchStatement";
    case AstKind::CaseStatement:
        return "CaseStatement";
    case AstKind::DefaultStatement:
        return "DefaultStatement";
    case AstKind::BreakStatement:
        return "BreakStatement";
    case AstKind::ContinueStatement:
        return "ContinueStatement";
    case AstKind::ReturnStatement:
        return "ReturnStatement";
    case AstKind::GotoStatement:
        return "GotoStatement";
    case AstKind::LabelStatement:
        return "LabelStatement";
    case AstKind::NullStatement:
        return "NullStatement";
    case AstKind::StaticAssertDeclaration:
        return "StaticAssertDeclaration";
    case AstKind::StructuredBinding:
        return "StructuredBinding";
    case AstKind::MemberInitializerList:
        return "MemberInitializerList";
    case AstKind::LambdaCapture:
        return "LambdaCapture";
    case AstKind::MemberInitializer:
        return "MemberInitializer";
    case AstKind::LambdaExpression:
        return "LambdaExpression";
    case AstKind::IntegerLiteral:
        return "IntegerLiteral";
    case AstKind::FloatLiteral:
        return "FloatLiteral";
    case AstKind::StringLiteral:
        return "StringLiteral";
    case AstKind::CharLiteral:
        return "CharLiteral";
    case AstKind::Identifier:
        return "Identifier";
    case AstKind::UnaryExpression:
        return "UnaryExpression";
    case AstKind::PostfixExpression:
        return "PostfixExpression";
    case AstKind::BinaryExpression:
        return "BinaryExpression";
    case AstKind::AssignmentExpression:
        return "AssignmentExpression";
    case AstKind::ConditionalExpression:
        return "ConditionalExpression";
    case AstKind::CallExpression:
        return "CallExpression";
    case AstKind::SubscriptExpression:
        return "SubscriptExpression";
    case AstKind::MemberExpression:
        return "MemberExpression";
    case AstKind::CastExpression:
        return "CastExpression";
    case AstKind::SizeofExpression:
        return "SizeofExpression";
    case AstKind::AlignofExpression:
        return "AlignofExpression";
    case AstKind::OffsetofExpression:
        return "OffsetofExpression";
    case AstKind::CompoundLiteral:
        return "CompoundLiteral";
    case AstKind::GenericSelection:
        return "GenericSelection";
    case AstKind::CommaExpression:
        return "CommaExpression";
    }
    return "Unknown";
}

std::unique_ptr<AstNode> make_ast(AstKind kind, SourceLocation location, std::string text) {
    auto node = std::make_unique<AstNode>();
    node->kind = kind;
    node->location = location;
    node->text = std::move(text);
    return node;
}

void dump_ast(const AstNode& node, std::ostream& out, int indent) {
    for (int i = 0; i < indent; ++i) {
        out << "  ";
    }
    out << ast_kind_name(node.kind);
    if (!node.text.empty()) {
        out << " '" << node.text << "'";
    }
    for (const auto& label : node.labels) {
        out << " [" << label << ']';
    }
    out << '\n';
    for (const auto& child : node.children) {
        if (child) {
            dump_ast(*child, out, indent + 1);
        }
    }
}

}  // namespace krc::frontend
