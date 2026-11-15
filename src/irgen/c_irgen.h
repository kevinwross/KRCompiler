#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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
        bool is_volatile = false;
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
    void collect_out_of_line_method(const frontend::AstNode& definition);
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
                          ir::TypePtr type, bool is_volatile = false);
    void emit_zero_initializer(ir::Value* address, ir::TypePtr type);
    void emit_vtable_init(ir::Value* object, ir::TypePtr type);
    [[nodiscard]] std::vector<std::string> virtual_slots(
        const frontend::RecordInfo& record) const;
    [[nodiscard]] int virtual_slot(const frontend::RecordInfo& record,
                                   std::string_view name) const;
    [[nodiscard]] bool method_is_virtual(const frontend::RecordInfo& record,
                                         std::string_view name) const;

    // Every call goes through here so that a call inside a `try` becomes an
    // invoke: the unwinder enters the landing pad instead of returning, and the
    // new normal block carries on where the call site left off.
    ir::Instruction* create_call_instruction(ir::Value* callee,
                                             std::vector<ir::Value*> arguments,
                                             ir::TypePtr result_type,
                                             const std::string& name);
    // An aggregate larger than eight bytes (or an FP-only aggregate larger than
    // sixteen) is memory class: it travels as a pointer to a caller-owned copy
    // (docs/abi.md, KR32 aggregate convention).
    [[nodiscard]] bool is_memory_class_argument(ir::TypePtr type) const;
    // The function type of a direct or indirect callee, or null.
    [[nodiscard]] ir::TypePtr call_signature(ir::Value* callee) const;
    // The IR parameter type: a memory-class aggregate parameter becomes a
    // hidden pointer, matching what map_function_type put in the signature.
    [[nodiscard]] ir::TypePtr parameter_ir_type(ir::TypePtr mapped) const;
    // An external runtime entry point, declared on first use.
    ir::Function* ensure_runtime_function(std::string_view name, ir::TypePtr result,
                                          const std::vector<ir::TypePtr>& parameters);
    void emit_try(const frontend::AstNode& statement);
    void emit_throw(const frontend::AstNode& statement);
    // The landing pad of the `try` currently being lowered, or null. Set for
    // the protected body and cleared while the handler itself is emitted.
    ir::BasicBlock* current_landing_pad_ = nullptr;
    // Destructor cleanup on propagation (ADR 0027): one cleanup landing pad per
    // lexical scope that owns a destructible automatic object. The pad runs
    // that scope's destructors and continues unwinding into the enclosing pad.
    struct LocalCleanup;
    struct ScopePad {
        std::size_t mark = 0;
        ir::BasicBlock* enclosing = nullptr;
    };
    std::vector<ScopePad> scope_pads_;
    // One pad per point that can unwind, each with the cleanups active at that
    // point. A single pad per scope cannot work: it is created before the later
    // locals exist and would destroy them from a block they do not dominate.
    struct CleanupPad {
        ir::BasicBlock* block = nullptr;
        std::vector<LocalCleanup> cleanups;
        ir::BasicBlock* enclosing = nullptr;
        // How many entries of local_cleanups_ this pad and its chain cover,
        // so a later pad destroys only what the chain has not covered yet.
        std::size_t covered = 0;
    };
    std::vector<CleanupPad> cleanup_pads_;
    // Emits every pad's landing pad, its destructors, and its resume, once the
    // function's blocks are known.
    void emit_cleanup_pads();
    // Creates the current scope's pad on its first destructible local, so a
    // call before that point is still protected by the enclosing pad.
    void note_scope_cleanup();
    // A pad only protects calls in its own function: synthetic constructors and
    // destructors are generated while another function's state is live, so the
    // pad is ignored when it belongs to a different function.
    [[nodiscard]] ir::BasicBlock* owned_landing_pad(ir::BasicBlock* pad) const;
    // Runs a list of cleanups in the given (already reverse-declaration) order.
    void emit_cleanup_sequence(const std::vector<LocalCleanup>& cleanups);
    // Emits the cleanups from mark to the end without consuming them: a
    // return path destroys what is live but leaves the list for the fall-through
    // path, which still reaches the scope's exit.
    void emit_cleanups_for_exit(std::size_t mark);
    // Continues unwinding from a pad: into the enclosing pad when there is one,
    // otherwise out of the frame.
    void emit_resume(ir::Value* exception);
    [[nodiscard]] Expression emit_expression(const frontend::AstNode& expression);    [[nodiscard]] Expression emit_lvalue(const frontend::AstNode& expression);
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
    // A call whose declared return type is a reference yields the referent, not
    // the address it returns: rray.at(0) == 5 compares values.
    // The address a reference-returning call answered with, so an assignment
    // through `front()` can write the element rather than a temporary.
    ir::Value* last_reference_address_ = nullptr;
    [[nodiscard]] Expression reference_result(ir::Value* call,
                                                 ir::Function* callee,
                                                 ir::TypePtr result_type);
    [[nodiscard]] Expression emit_matrix_intrinsic(const frontend::AstNode& expression,
                                                   const ir::Function& callee,
                                                   ir::Intrinsic intrinsic);
    [[nodiscard]] Expression emit_subscript(const frontend::AstNode& expression);
    // The begin()/end() form of a range-for over a class.
    bool emit_range_for_class(const frontend::AstNode& statement,
                              ir::Function& begin_method,
                              ir::Function& end_method,
                              const std::string& value_name,
                              bool bound_by_reference,
                              ir::TypePtr element_ir,
                              frontend::TypePtr element_type);
    // Calls a record's operator[] with an already-emitted object address, so
    // s[i] and (*this)[i] share one path.
    [[nodiscard]] Expression unary_operator_call(ir::Function* method_function,
                                              Expression self,
                                              bool as_address);
    [[nodiscard]] ir::Function* arrow_overload(const Expression& object);
    [[nodiscard]] Expression arrow_base(const frontend::AstNode& operand,
                                      ir::TypePtr& record_type);
    [[nodiscard]] ir::Function* dereference_overload(const Expression& object);
    [[nodiscard]] Expression binary_operator_call(ir::Function* method_function,
                                                 Expression self,
                                                 const frontend::AstNode& right);
    [[nodiscard]] Expression subscript_operator(ir::Function* function,
                                                 Expression self,
                                                 const frontend::AstNode& index_node,
                                                 bool as_address = false);
    [[nodiscard]] Expression emit_member(const frontend::AstNode& expression);
    [[nodiscard]] Expression emit_member_lvalue(const frontend::AstNode& expression);
    [[nodiscard]] Expression emit_implicit_member(std::string_view name);
    [[nodiscard]] Expression emit_implicit_member_lvalue(std::string_view name);
    [[nodiscard]] Expression emit_cast(const frontend::AstNode& expression);
    [[nodiscard]] Expression emit_dynamic_cast(const frontend::AstNode& expression);
    // Allocates and default-constructs a class object, then throws it. Used by
    // a reference dynamic_cast, which throws std::bad_cast when it fails.
    void throw_object(const frontend::RecordInfo& record,
                      const frontend::SourceLocation& location);
    // `typeid` yields an lvalue of the type_info record: the address of the
    // object's dynamic typeinfo for a polymorphic operand, or of the static
    // typeinfo otherwise.
    [[nodiscard]] Expression emit_typeid_reference(const frontend::AstNode& expression);
    [[nodiscard]] Expression emit_typeid(const frontend::AstNode& expression);
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
    std::unordered_map<ir::GlobalVariable*, frontend::TypePtr>
        global_frontend_types_;
    std::unordered_set<ir::GlobalVariable*> volatile_globals_;
    struct GlobalCtorInfo {
        ir::GlobalVariable* global = nullptr;
        const frontend::RecordInfo* record = nullptr;
        std::vector<const frontend::AstNode*> arguments;
        bool default_construct = false;
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
    // Read-only class metadata: _ZTS<tag> holds the name, _ZTI<tag> the
    // type_info, and _ZTV<tag> the vtable with the typeinfo address at word 0
    // so a vptr points at word 1 (RTTI at vptr - 4, virtual slot k at
    // vptr + 4*k).
    [[nodiscard]] ir::GlobalVariable* ensure_typeinfo(
        const frontend::RecordInfo& record);
    [[nodiscard]] ir::GlobalVariable* ensure_vtable(
        const frontend::RecordInfo& record);
    [[nodiscard]] ir::Function* vtable_slot_implementation(
        const frontend::RecordInfo& record, std::string_view slot);
    void finalize_vtables();
    struct PendingVtable {
        const frontend::RecordInfo* record = nullptr;
        ir::GlobalVariable* table = nullptr;
    };
    std::vector<PendingVtable> pending_vtables_;
    std::unordered_map<const frontend::RecordInfo*, ir::GlobalVariable*>
        typeinfo_globals_;
    std::unordered_map<const frontend::RecordInfo*, ir::GlobalVariable*>
        vtable_globals_;
    ir::Function* ensure_member_destructor(
        const frontend::RecordInfo* record);
    ir::Function* ensure_default_constructor(
        const frontend::RecordInfo* record);
    std::unordered_map<const frontend::RecordInfo*, ir::Function*>
        member_destructors_;
    std::unordered_map<const frontend::RecordInfo*, ir::Function*>
        default_constructors_;
    std::unordered_map<const frontend::RecordInfo*, ir::TypePtr> record_types_;
    std::unordered_map<const ir::Type*, const frontend::RecordInfo*> ir_records_;
    std::unordered_map<std::string, std::unordered_map<std::string, ir::Function*>>
        class_methods_;
    std::unordered_map<const ir::Function*, const frontend::RecordInfo*> function_classes_;
    std::unordered_set<const ir::Function*> static_methods_;
    std::vector<std::pair<ir::Function*, const frontend::AstNode*>> method_bodies_;
    struct LocalCleanup {
        ir::Value* address = nullptr;
        ir::Function* destructor = nullptr;
        std::uint64_t count = 1;
        ir::TypePtr element;
    };
    std::vector<LocalCleanup> local_cleanups_;    std::vector<std::size_t> cleanup_marks_;
    const frontend::RecordInfo* current_class_ = nullptr;
    bool in_static_method_ = false;
    bool in_destructor_ = false;
    bool in_constructor_ = false;
    std::vector<const frontend::AstNode*> definitions_;
    std::vector<std::unordered_map<std::string, LocalBinding>> scopes_;
    // A jump target also names the cleanup count at the loop it leaves, so a
    // break or continue destroys the objects of the scopes it exits.
    struct JumpTarget {
        ir::BasicBlock* block = nullptr;
        std::size_t mark = 0;
    };
    std::vector<JumpTarget> break_targets_;
    std::vector<JumpTarget> continue_targets_;
    std::unordered_map<std::string, ir::BasicBlock*> labels_;
    ir::Function* current_function_ = nullptr;
    ir::IRBuilder builder_;
    std::uint64_t string_counter_ = 0;
    std::uint64_t record_counter_ = 0;
    bool had_error_ = false;
};

}  // namespace krc::irgen
