#pragma once

#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "krc/frontend/source_manager.h"

namespace krc::frontend {

enum class AstKind {
    TranslationUnit,
    Declaration,
    DeclSpecifiers,
    AlignasSpecifier,
    InitDeclarator,
    FunctionDefinition,
    Declarator,
    PointerDeclarator,
    ReferenceDeclarator,
    ArrayDeclarator,
    FunctionDeclarator,
    ParameterDeclaration,
    MemberDeclaration,
    StructSpecifier,
    UnionSpecifier,
    EnumSpecifier,
    Enumerator,
    TypeName,
    InitializerList,
    DesignatedInitializer,
    CompoundStatement,
    DeclarationStatement,
    ExpressionStatement,
    IfStatement,
    WhileStatement,
    DoStatement,
    ForStatement,
    RangeForStatement,
    SwitchStatement,
    CaseStatement,
    DefaultStatement,
    BreakStatement,
    ContinueStatement,
    ReturnStatement,
    GotoStatement,
    LabelStatement,
    NullStatement,
    StaticAssertDeclaration,
    StructuredBinding,
    MemberInitializerList,
    MemberInitializer,
    DefaultMemberInitializer,
    LambdaExpression,
    LambdaCapture,
    IntegerLiteral,
    FloatLiteral,
    StringLiteral,
    CharLiteral,
    Identifier,
    UnaryExpression,
    PostfixExpression,
    BinaryExpression,
    AssignmentExpression,
    ConditionalExpression,
    CallExpression,
    SubscriptExpression,
    MemberExpression,
    CastExpression,
    SizeofExpression,
    AlignofExpression,
    OffsetofExpression,
    CompoundLiteral,
    GenericSelection,
    CommaExpression,
};

struct AstNode {
    AstKind kind = AstKind::NullStatement;
    SourceLocation location;
    std::string text;
    std::vector<std::string> labels;
    std::vector<std::unique_ptr<AstNode>> children;
};

[[nodiscard]] std::string_view ast_kind_name(AstKind kind) noexcept;
[[nodiscard]] std::unique_ptr<AstNode> make_ast(AstKind kind, SourceLocation location,
                                                std::string text = {});
void dump_ast(const AstNode& node, std::ostream& out, int indent = 0);

}  // namespace krc::frontend
