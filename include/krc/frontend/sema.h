#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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
    // The definitions produced by instantiation, for code generation.
    [[nodiscard]] const std::vector<const AstNode*>& instantiations() const {
        return instantiations_;
    }
    // The instantiated class specifiers, for code generation.
    [[nodiscard]] const std::vector<const AstNode*>& class_instantiations() const {
        return class_instantiations_;
    }
    // True when the tag names an instantiated class template.
    [[nodiscard]] bool is_class_instance(std::string_view tag) const {
        return class_instances_.count(std::string(tag)) != 0;
    }
    // True when a function definition is a template pattern with no analysable
    // body: its body depends on a value parameter, on a parameter pack, or on a
    // pack in the signature (`arity_of(Tup<T, Rest...>&)`), none of which has a
    // value until a call supplies one. Such a pattern is a signature only, so it
    // has no body to analyse and none to emit -- the instances are the definitions.
    // Sema and code generation must ask this one question, or one of them skips a
    // pattern the other still builds.
    [[nodiscard]] bool is_pattern_signature(const AstNode& definition) const;
    // The value a sizeof...(pack) folded to, for code generation.
    [[nodiscard]] bool folded_constant(const AstNode& expression,
                                      long long& value) const {
        const auto found = constant_folds_.find(&expression);
        if (found == constant_folds_.end()) {
            return false;
        }
        value = found->second;
        return true;
    }
    // The name a call resolved to when its callee was a template, or empty.
    [[nodiscard]] std::string call_target(const AstNode& call) const {
        const auto found = call_targets_.find(&call);
        return found == call_targets_.end() ? std::string{} : found->second;
    }
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
    [[nodiscard]] TypePtr typeid_operand_type(const AstNode& expression) const {
        const auto found = typeid_operands_.find(&expression);
        return found == typeid_operands_.end() ? nullptr : found->second;
    }
    [[nodiscard]] TypePtr cast_target_type(const AstNode& expression) const {
        const auto found = cast_targets_.find(&expression);
        return found == cast_targets_.end() ? nullptr : found->second;
    }
    [[nodiscard]] TypePtr cast_operand_type(const AstNode& expression) const {
        const auto found = cast_operands_.find(&expression);
        return found == cast_operands_.end() ? nullptr : found->second;
    }
    [[nodiscard]] TypePtr bool_type() const { return bool_type_; }
    // Resolves a name node that may carry template arguments, as a constructor
    // call does: `Box<T>(value)` names `Box` with a template-arg label, and a
    // plain spelling lookup would answer with the unparameterised record. Callers
    // that have a node rather than a string must use this, not type_named.
    [[nodiscard]] TypePtr type_of_name(const AstNode& node);
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
    // Gives a class member's signature one parameter per pack element and rewrites
    // the arguments that forward the pack to match. See the definition for why both
    // halves are needed together.
    void expand_member_packs(
        AstNode& specifier,
        const std::unordered_map<std::string, std::size_t>& arities);

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
    // The type a declaration resolved to while its bindings were active. A
    // class-template instance's body is analysed then, so re-deriving the type
    // later would resolve a parameter to the pattern's stub (ADR 0035).
    std::unordered_map<const AstNode*, TypePtr> declarator_types_;
    // The type each `typeid` operand was analysed with. IR generation runs
    // after the function's scopes are gone, so it cannot re-analyse the
    // operand and must read the recorded type.
    std::unordered_map<const AstNode*, TypePtr> typeid_operands_;
    // The target and operand types of a `dynamic_cast`, recorded for the same
    // reason. Other named casts lower from the AST directly.
    std::unordered_map<const AstNode*, TypePtr> cast_targets_;
    std::unordered_map<const AstNode*, TypePtr> cast_operands_;
    // A function template pattern: its parameter names and the definition the
    // parser recorded. The definition is also analyzed with the int
    // substitution the parser installs, so an all-int call needs no
    // instantiation. (ADR 0031)
    struct TemplateInfo {
        std::vector<std::string> parameters;
        const AstNode* definition = nullptr;
        std::string name;
        // A class template's definition is its StructSpecifier; a function
        // template's is its FunctionDefinition.
        bool is_class = false;
        // Parameters written with a type before the name (int N) stand for a
        // number, and are substituted by a literal in the instance (ADR 0033).
        std::unordered_set<std::string> value_parameters;
    };
    std::unordered_map<std::string, TemplateInfo> templates_;
    // Partial specializations, keyed by the primary's name and described by the
    // parameters they fix before their pack: `Tuple<T, Rest...>` fixes one, so it
    // matches any arity of at least one and the primary keeps arity zero. That is
    // the whole of the ordering this slice supports -- argument count, not a
    // general partial order.
    std::unordered_map<std::string, std::vector<TemplateInfo>> specializations_;
    // Template parameter bindings active while an instantiation is analyzed.
    std::unordered_map<std::string, TypePtr> template_bindings_;
    // Value parameters bound while a pattern's fallback or an instance is
    // analysed, so a bound name folds to its number (ADR 0033).
    std::unordered_map<std::string, long long> template_value_bindings_;
    // A constant an expression folded to, keyed by its node: code generation
    // runs after the bindings are gone and reads the value here (ADR 0033).
    std::unordered_map<const AstNode*, long long> constant_folds_;
    // A pack's arity while its instance is analysed, so sizeof...(pack) folds
    // to the count the call supplied.
    std::unordered_map<std::string, std::size_t> template_pack_arities_;
    // Definitions created by instantiation, in the order they were created.
    std::vector<const AstNode*> instantiations_;
    // The function a call resolves to when the callee is a template name.
    std::unordered_map<const AstNode*, std::string> call_targets_;
    // Instantiated names, so a repeated call reuses one definition.
    std::unordered_set<std::string> instantiated_names_;
    // Keeps each instantiated definition alive for the lifetime of Sema.
    std::vector<std::unique_ptr<AstNode>> instantiation_storage_;
    // Instantiated class specifiers, kept alive for code generation, and the
    // (tag, arguments) to type map that makes a repeat reference reuse one.
    std::vector<const AstNode*> class_instantiations_;
    std::vector<std::unique_ptr<AstNode>> class_instantiation_storage_;
    std::unordered_map<std::string, TypePtr> class_instances_;
    // The argument list each class instance was built from, keyed by its tag. A
    // function template can name a class template in its signature
    // (`arity_of(Tup<T, Rest...>&)`), and the only place the argument types exist
    // is here: the instance's own tag is a serialized name, not a list.
    std::unordered_map<std::string, std::vector<TypePtr>> class_instance_arguments_;
    // Builds (or finds) the record for 	ag<arguments> from the class template
    // pattern, or returns null when the pattern or an argument is unusable.
    [[nodiscard]] TypePtr instantiate_class_template(
        const std::string& tag, const std::vector<std::string>& arguments);
    // Analyzes a clone of the named template with the deduced bindings and
    // returns the instantiated function's name, or empty when the callee is not
    // a template or the pattern is outside the supported subset (ADR 0031).
    [[nodiscard]] std::string instantiate_template_call(
        const AstNode& callee, const std::vector<std::string>& explicit_arguments,
        const std::vector<TypePtr>& arguments);
    // Binds a function template's parameters from an argument whose type is an
    // instance of the class template its parameter's specifier names, which is
    // what `arity_of(Tup<T, Rest...>&)` needs. The match is positional against the
    // instance's recorded argument list: the specifier's i-th `template-arg:`
    // spelling names the parameter the instance's i-th argument supplies, and a
    // trailing `Name...` takes the remainder as a pack. Returns false when the
    // shape does not apply, leaving ordinary deduction in charge.
    [[nodiscard]] bool deduce_class_template_arguments(
        const AstNode& specifier, const TypePtr& argument,
        const std::vector<std::string>& parameters,
        std::unordered_map<std::string, TypePtr>& bindings,
        std::unordered_map<std::string, std::size_t>& pack_arities) const;
    // Resolves the spelling of an explicit template argument (double) to a
    // type, or null when it is not a type this front end knows.
    [[nodiscard]] TypePtr template_argument_type(const std::string& spelling) const;
    // Replaces a pack-expansion argument (`Rest...`) with one argument per element
    // of the enclosing instance's pack, named the way instantiation binds them
    // (`Rest0`, `Rest1`). An argument list carrying no expansion is returned
    // unchanged, and a pack of arity zero expands to nothing.
    [[nodiscard]] std::vector<std::string> expand_template_arguments(
        const std::vector<std::string>& arguments) const;
    // Non-zero while a catch body is being analysed; `throw;` needs a handler.
    int handler_depth_ = 0;
    void check_dynamic_cast(const AstNode& expression, TypePtr target, TypePtr source);
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
