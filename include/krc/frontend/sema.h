#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "krc/frontend/ast.h"
#include "krc/frontend/diagnostics.h"
#include "krc/frontend/source_manager.h"
#include "krc/frontend/types.h"
#include "krc/target/target.h"

namespace krc::frontend {

class Sema {
public:
    Sema(DiagnosticEngine& diagnostics, const target::TargetProfile& profile,
         bool cxx_mode = false);
    ~Sema();

    void analyze(const AstNode& translation_unit);
    void break_cycles();
    [[nodiscard]] bool had_error() const { return had_error_; }

    struct GlobalSymbol {
        enum class Kind {
            Variable,
            Function,
            Typedef,
        };

        Kind kind = Kind::Variable;
        TypePtr type;
        bool defined = false;
    };

    [[nodiscard]] std::optional<GlobalSymbol> global_symbol(std::string_view name) const;
    [[nodiscard]] TypePtr declaration_type(const AstNode& specifiers,
                                           const AstNode* declarator);
    [[nodiscard]] TypePtr type_from_specifiers(const AstNode& specifiers);
    [[nodiscard]] TypePtr type_from_type_name_node(const AstNode& type_name);
    [[nodiscard]] std::uint64_t size_of_type(TypePtr type) const;
    [[nodiscard]] std::uint64_t align_of_type(TypePtr type) const;
    [[nodiscard]] bool evaluate_constant_expression(const AstNode& expression,
                                                    long long& value);

    [[nodiscard]] TypePtr expression_type(const AstNode& expression) {
        const auto cached = expression_types_.find(&expression);
        if (cached != expression_types_.end()) {
            return cached->second;
        }
        return analyze_expression(expression);
    }
    [[nodiscard]] TypePtr bool_type() const { return bool_type_; }
    [[nodiscard]] TypePtr type_named(std::string_view name) {
        if (name == "bool" || name == "_Bool") {
            return bool_type_;
        }
        if (name == "int") {
            return int_type_;
        }
        if (name == "char") {
            return char_type_;
        }
        if (name == "short") {
            return short_type_;
        }
        if (name == "long") {
            return long_type_;
        }
        if (name == "float") {
            return float_type_;
        }
        if (name == "double") {
            return double_type_;
        }
        if (auto* symbol = lookup_tag(name)) {
            return symbol->type;
        }
        if (auto* symbol = lookup_ordinary(name)) {
            return symbol->type;
        }
        return nullptr;
    }
    [[nodiscard]] TypePtr type_from_type_name(const AstNode& type_name) {
        return analyze_type_name(type_name);
    }
    [[nodiscard]] std::vector<std::uint64_t> record_member_offsets(const RecordInfo& record) const {
        return member_offsets(record);
    }

private:
    enum class SymbolKind {
        Variable,
        Function,
        Typedef,
        EnumConstant,
        RecordTag,
        EnumTag,
    };

    struct Symbol {
        SymbolKind kind = SymbolKind::Variable;
        std::string name;
        TypePtr type;
        SourceLocation location;
        bool defined = false;
        long long constant_value = 0;
        bool is_constant_value = false;
    };

    struct Scope {
        std::unordered_map<std::string, Symbol> ordinary;
        std::unordered_map<std::string, Symbol> tags;
    };

    void push_scope();
    void pop_scope();
    [[nodiscard]] bool at_global_scope() const;
    [[nodiscard]] Symbol* lookup_ordinary(std::string_view name);
    [[nodiscard]] Symbol* lookup_tag(std::string_view name);
    bool declare_ordinary(Symbol symbol);
    bool declare_tag(Symbol symbol);

    [[nodiscard]] TypePtr make_builtin(TypeKind kind, bool is_unsigned = false) const;
    [[nodiscard]] TypePtr pointer_to(TypePtr element) const;
    [[nodiscard]] TypePtr array_of(TypePtr element, std::uint64_t count,
                                   bool has_count) const;
    [[nodiscard]] TypePtr function_of(TypePtr result, std::vector<TypePtr> parameters,
                                      bool variadic, bool has_prototype) const;
    [[nodiscard]] TypePtr qualify(TypePtr type, bool is_const, bool is_volatile) const;
    [[nodiscard]] TypePtr decay(TypePtr type) const;
    [[nodiscard]] TypePtr promote(TypePtr type) const;
    [[nodiscard]] TypePtr usual_arithmetic(TypePtr left, TypePtr right) const;
    [[nodiscard]] int rank(const Type& type) const;

    [[nodiscard]] TypePtr base_type_from_specifiers(const AstNode& specifiers,
                                                    bool& is_typedef, bool& is_extern,
                                                    bool& is_static);
    [[nodiscard]] TypePtr record_type(const AstNode& specifier, bool is_union);
    [[nodiscard]] TypePtr enum_type(const AstNode& specifier);
    void analyze_member_declaration(const AstNode& member, RecordInfo& record);

    void collect_declarator(const AstNode& declarator,
                            std::vector<const AstNode*>& operations, std::string& name) const;
    [[nodiscard]] TypePtr build_declarator_type(const AstNode& declarator, TypePtr base);
    [[nodiscard]] TypePtr apply_declarator_operations(
        const std::vector<const AstNode*>& operations, TypePtr base);
    [[nodiscard]] TypePtr parameter_type(const AstNode& parameter);
    [[nodiscard]] const AstNode* find_function_declarator(const AstNode& declarator) const;
    [[nodiscard]] std::string declarator_name(const AstNode& declarator) const;

    void analyze_declaration(const AstNode& declaration);
    void analyze_function_definition(const AstNode& definition);
    // Analyses the parameters and body of a function definition in a fresh
    // scope. A record gives the body class scope, `this`, and its member
    // initializers.
    void analyze_function_body(const AstNode& definition, RecordInfo* record);
    void analyze_compound(const AstNode& compound);
    void analyze_statement(const AstNode& statement);
    void analyze_static_assert(const AstNode& assertion);
    void analyze_parameters(const AstNode& function_declarator);

    TypePtr analyze_expression(const AstNode& expression);
    TypePtr analyze_initializer(const AstNode& initializer, TypePtr target);
    [[nodiscard]] TypePtr analyze_type_name(const AstNode& type_name);

    [[nodiscard]] bool evaluate_constant(const AstNode& expression, long long& value);
    [[nodiscard]] bool evaluate_constexpr_body(const AstNode& statement, long long& value);
    std::unordered_map<std::string, const AstNode*> constexpr_functions_;
    std::unordered_map<std::string, long long> constant_locals_;
    std::size_t constant_depth_ = 0;
    bool auto_return_active_ = false;
    TypePtr auto_return_type_;
    std::unordered_map<const AstNode*, TypePtr> expression_types_;
    [[nodiscard]] std::optional<long long> parse_integer_literal(std::string_view text) const;
    [[nodiscard]] std::optional<long long> parse_character_literal(
        std::string_view text) const;
    [[nodiscard]] std::uint64_t size_of(const Type& type) const;
    [[nodiscard]] std::uint64_t align_of(const Type& type) const;
    [[nodiscard]] std::vector<std::uint64_t> member_offsets(const RecordInfo& record) const;
    [[nodiscard]] std::optional<std::uint64_t> offset_of(
        const Type& type, const std::vector<std::string>& path) const;

    void error(const SourceLocation& location, std::string message);
    void warning(const SourceLocation& location, std::string message);
    // Reports a non-public member reached from outside the declaring record
    // unless the enclosing function is one of its friends.
    void check_member_access(const SourceLocation& location, const RecordInfo& record,
                             const std::string& name, AccessLevel access);

    DiagnosticEngine& diagnostics_;
    const target::TargetProfile& profile_;
    bool cxx_mode_ = false;

    TypePtr void_type_;
    TypePtr bool_type_;
    TypePtr char_type_;
    TypePtr short_type_;
    TypePtr int_type_;
    TypePtr long_type_;
    TypePtr long_long_type_;
    TypePtr float_type_;
    TypePtr double_type_;
    TypePtr long_double_type_;
    TypePtr size_type_;
    TypePtr ptrdiff_type_;
    TypePtr intmax_type_;

    std::vector<Scope> scopes_;
    std::vector<std::weak_ptr<RecordInfo>> record_infos_;
    std::string current_function_name_;
    // The class named by an out-of-line member definition (`S::get`), null in a
    // free function. It gives the body class scope and `this`, and lets the
    // access check recognize the defining class.
    RecordInfo* enclosing_record_ = nullptr;
    bool had_error_ = false;
};

}  // namespace krc::frontend
