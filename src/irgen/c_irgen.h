#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ir/ir.h"
#include "krc/frontend/ast.h"
#include "krc/frontend/diagnostics.h"
#include "krc/frontend/sema.h"
#include "krc/frontend/types.h"
#include "krc/target/target.h"

namespace krc::irgen {

class CIRGen {
public:
    CIRGen(frontend::Sema& sema, const frontend::AstNode& translation_unit,
           const target::TargetProfile& profile, frontend::DiagnosticEngine& diagnostics);

    [[nodiscard]] std::unique_ptr<ir::Module> generate();
    [[nodiscard]] bool had_error() const { return had_error_; }

private:
    struct LocalBinding {
        ir::Value* address = nullptr;
        ir::TypePtr type = nullptr;
        frontend::TypePtr frontend_type;
    };

    struct Expression {
        ir::Value* value = nullptr;
        ir::TypePtr type = nullptr;
        bool bit_field = false;
        bool reference_member = false;
        std::uint32_t bit_offset = 0;
        std::uint32_t bit_width = 0;
    };

    void collect_declarations();
    void collect_function(const frontend::AstNode& specifiers,
                          const frontend::AstNode& declarator, bool definition,
                          const frontend::AstNode* body);
    void collect_global(const frontend::AstNode& specifiers,
                        const frontend::AstNode& declarator,
                        const frontend::AstNode* initializer);
    void collect_class_methods(const frontend::AstNode& specifiers);
    void record_defaults(ir::Function* function, const frontend::AstNode& declarator);
    [[nodiscard]] ir::Function* select_constructor(const std::string& tag,
                                                   std::size_t argument_count,
                                                   int reference_mode = 0) const;
    [[nodiscard]] ir::Function* select_assignment(const std::string& tag,
                                                  int reference_mode) const;
    void clone_inherited_methods();
    void generate_function(const frontend::AstNode& definition,
                           ir::Function* method_function = nullptr);

    [[nodiscard]] ir::TypePtr map_type(frontend::TypePtr type);
    [[nodiscard]] ir::TypePtr map_function_type(frontend::TypePtr type);
    [[nodiscard]] ir::TypePtr map_record(const frontend::RecordInfo& record);

    void push_scope();
    void pop_scope();
    [[nodiscard]] LocalBinding* lookup_local(std::string_view name);
    [[nodiscard]] ir::Function* lookup_function(std::string_view name) const;
    [[nodiscard]] ir::GlobalVariable* lookup_global(std::string_view name) const;
    void declare_local(std::string name, LocalBinding binding);

    void emit_statement(const frontend::AstNode& statement);
    void emit_declaration(const frontend::AstNode& declaration);
    void emit_compound(const frontend::AstNode& compound);
    void emit_initializer(const frontend::AstNode& initializer, ir::Value* address,
                          ir::TypePtr type);
    void emit_zero_initializer(ir::Value* address, ir::TypePtr type);
    void emit_vtable_init(ir::Value* object, ir::TypePtr type);
    [[nodiscard]] std::vector<std::string> virtual_slots(
        const frontend::RecordInfo& record) const;
    [[nodiscard]] int virtual_slot(const frontend::RecordInfo& record,
                                   std::string_view name) const;
    [[nodiscard]] bool method_is_virtual(const frontend::RecordInfo& record,
                                         std::string_view name) const;

    [[nodiscard]] Expression emit_expression(const frontend::AstNode& expression);
    [[nodiscard]] Expression emit_lvalue(const frontend::AstNode& expression);
    [[nodiscard]] Expression emit_condition_expression(const frontend::AstNode& expression);
    [[nodiscard]] Expression emit_lambda(const frontend::AstNode& expression);
    void emit_cleanups();
    void emit_cleanups_to(std::size_t mark);
    void emit_copy(ir::Value* destination, ir::Value* source, ir::TypePtr type);
    [[nodiscard]] Expression emit_integer_literal(const frontend::AstNode& expression);
    [[nodiscard]] Expression emit_string_literal(const frontend::AstNode& expression);
    [[nodiscard]] Expression emit_binary(const frontend::AstNode& expression);
    [[nodiscard]] Expression emit_unary(const frontend::AstNode& expression);
    [[nodiscard]] Expression emit_assignment(const frontend::AstNode& expression);
    [[nodiscard]] Expression emit_call(const frontend::AstNode& expression);
    [[nodiscard]] Expression emit_subscript(const frontend::AstNode& expression);
    [[nodiscard]] Expression emit_member(const frontend::AstNode& expression);
    [[nodiscard]] Expression emit_member_lvalue(const frontend::AstNode& expression);
    [[nodiscard]] Expression emit_implicit_member(std::string_view name);
    [[nodiscard]] Expression emit_implicit_member_lvalue(std::string_view name);
    [[nodiscard]] Expression emit_cast(const frontend::AstNode& expression);
    [[nodiscard]] Expression emit_sizeof(const frontend::AstNode& expression);

    [[nodiscard]] ir::Value* emit_condition(const frontend::AstNode& expression);
    void emit_branch(ir::BasicBlock* target);
    void emit_return(const frontend::AstNode& statement);

    [[nodiscard]] ir::Value* promote(ir::Value* value, ir::TypePtr from, ir::TypePtr to);
    [[nodiscard]] ir::Value* constant_int(ir::TypePtr type, std::int64_t value);
    [[nodiscard]] ir::TypePtr type_for_integer_literal(std::string_view text);
    [[nodiscard]] std::string decode_string(std::string_view text) const;
    [[nodiscard]] std::string declarator_name(const frontend::AstNode& declarator) const;
    [[nodiscard]] const frontend::AstNode* find_function_declarator(
        const frontend::AstNode& declarator) const;
    [[nodiscard]] std::string parameter_name(const frontend::AstNode& parameter,
                                             std::size_t index) const;
    [[nodiscard]] bool has_label(const frontend::AstNode& specifiers,
                                 std::string_view label) const;
    [[nodiscard]] ir::TypePtr size_type() const;
    [[nodiscard]] ir::TypePtr int_type() const;
    [[nodiscard]] std::uint64_t ir_size_of(ir::TypePtr type) const;
    [[nodiscard]] std::uint32_t alignment_of(frontend::TypePtr type) const;
    [[nodiscard]] std::uint32_t alignment_of(const frontend::AstNode& specifiers,
                                            frontend::TypePtr type) const;

    void error(const frontend::SourceLocation& location, std::string message);

    frontend::Sema& sema_;
    const frontend::AstNode& unit_;
    const target::TargetProfile& profile_;
    frontend::DiagnosticEngine& diagnostics_;
    std::unique_ptr<ir::Module> module_;
    std::unordered_map<std::string, ir::Function*> functions_;
    std::unordered_map<std::string, std::vector<ir::Function*>> class_constructors_;
    std::unordered_map<std::string, std::vector<ir::Function*>> class_assignments_;
    std::unordered_map<std::string, std::vector<ir::Function*>> class_incdec_;
    std::unordered_map<ir::Function*, std::vector<const frontend::AstNode*>> function_defaults_;
    std::unordered_map<ir::Function*, frontend::TypePtr> function_ast_types_;
    std::unordered_map<ir::Function*, std::string> function_source_names_;
    std::size_t lambda_counter_ = 0;
    std::size_t static_local_counter_ = 0;
    struct LambdaInfo {
        ir::TypePtr closure;
        ir::TypePtr function;
    };
    std::unordered_map<const frontend::AstNode*, LambdaInfo> lambda_metadata_;
    std::unordered_map<std::string, LambdaInfo> lambda_locals_;
    std::unordered_map<std::string, ir::GlobalVariable*> globals_;
    struct GlobalCtorInfo {
        ir::GlobalVariable* global = nullptr;
        const frontend::RecordInfo* record = nullptr;
        std::vector<const frontend::AstNode*> arguments;
    };
    std::vector<GlobalCtorInfo> global_ctors_;
    struct GlobalInitInfo {
        ir::GlobalVariable* global = nullptr;
        const frontend::AstNode* expression = nullptr;
    };
    std::vector<GlobalInitInfo> global_initializers_;
    std::vector<std::pair<ir::GlobalVariable*, const frontend::RecordInfo*>>
        local_static_dtors_;
    void generate_global_constructors();
    std::unordered_map<const frontend::RecordInfo*, ir::TypePtr> record_types_;
    std::unordered_map<const ir::Type*, const frontend::RecordInfo*> ir_records_;
    std::unordered_map<std::string, std::unordered_map<std::string, ir::Function*>>
        class_methods_;
    std::unordered_map<const ir::Function*, const frontend::RecordInfo*> function_classes_;
    std::vector<std::pair<ir::Function*, const frontend::AstNode*>> method_bodies_;
    std::vector<std::pair<ir::Value*, ir::Function*>> local_cleanups_;
    std::vector<std::size_t> cleanup_marks_;
    const frontend::RecordInfo* current_class_ = nullptr;
    std::vector<const frontend::AstNode*> definitions_;
    std::vector<std::unordered_map<std::string, LocalBinding>> scopes_;
    std::vector<ir::BasicBlock*> break_targets_;
    std::vector<ir::BasicBlock*> continue_targets_;
    ir::Function* current_function_ = nullptr;
    ir::IRBuilder builder_;
    std::uint64_t string_counter_ = 0;
    std::uint64_t record_counter_ = 0;
    bool had_error_ = false;
};

}  // namespace krc::irgen
