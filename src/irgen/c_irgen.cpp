#include "irgen/c_irgen.h"

#include <algorithm>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "irgen/c_irgen_internal.h"

namespace krc::irgen {

void CIRGen::generate_function(const frontend::AstNode& definition,
                               ir::Function* method_function) {
    if (definition.children.size() < 3) {
        return;
    }
    const auto* declarator = definition.children[1].get();
    const auto* body = definition.children[2].get();
    if (!declarator || !body) {
        return;
    }
    const auto name = declarator_name(*declarator);
    ir::Function* function = method_function;
    if (!function) {
        const auto found = functions_.find(name);
        if (found == functions_.end()) {
            return;
        }
        function = found->second;
    }
    function->declaration = false;
    // The definition's own line is front-end knowledge; the backends cannot
    // recover it from the first statement, which is a later line. A synthesized
    // body has no location and leaves the field empty (ADR 0061).
    if (definition.location.valid()) {
        const auto location =
            diagnostics_.source_manager().line_column(definition.location);
        function->definition.file =
            std::string(diagnostics_.source_manager().file_name(definition.location));
        function->definition.line = location.line;
        function->definition.column = location.column;
    }
    if (!function->blocks.empty()) {
        error(definition.location, "function '" + name + "' already has a body");
        return;
    }

    const auto previous_class = current_class_;
    const auto previous_destructor = in_destructor_;
    const auto previous_constructor = in_constructor_;
    const auto previous_static_method = in_static_method_;
    const auto class_found = function_classes_.find(function);
    current_class_ = class_found == function_classes_.end() ? nullptr : class_found->second;
    in_static_method_ = static_methods_.contains(function);
    in_destructor_ =
        function->name.starts_with("~") || function->name.ends_with("_dtor");
    const bool emitting_constructor =
        function->name.find("_ctor_") != std::string::npos;
    in_constructor_ = emitting_constructor;
    current_function_ = function;
    auto* entry = function->add_block("entry");
    builder_.reset_value_names();
    builder_.set_insert_point(entry);
    scopes_.clear();
    push_scope();
    local_cleanups_.clear();
    cleanup_marks_.clear();
    scope_pads_.clear();
    cleanup_pads_.clear();
    current_landing_pad_ = nullptr;

    frontend::TypePtr frontend_function_type;
    if (definition.children.size() > 1 && definition.children[0] &&
        definition.children[1]) {
        frontend_function_type =
            sema_.declaration_type(*definition.children[0], definition.children[1].get());
    }

    for (std::size_t index = 0; index < function->parameters.size(); ++index) {
        const auto& parameter = function->parameters[index];
        frontend::TypePtr parameter_type;
        if (frontend_function_type &&
            frontend_function_type->kind == frontend::TypeKind::Function) {
            const auto frontend_index = current_class_ ? index - 1 : index;
            if (!current_class_ || index > 0) {
                if (frontend_index < frontend_function_type->parameters.size()) {
                    parameter_type = frontend_function_type->parameters[frontend_index];
                }
            }
        }
        // A memory-class aggregate arrives as a pointer to the caller's copy;
        // the callee works on its own copy, as the ABI allows it to modify it.
        const bool indirect =
            parameter_type && parameter_type->kind == frontend::TypeKind::Record &&
            parameter->type && parameter->type->kind == ir::TypeKind::Pointer &&
            parameter->type->element;
        auto* storage_type = indirect ? parameter->type->element : parameter->type;
        auto* address = builder_.create_alloca(storage_type, 0, parameter->name + ".addr");
        if (indirect) {
            emit_copy(address, parameter.get(), storage_type);
        } else {
            builder_.create_store(parameter.get(), address);
        }
        declare_local(parameter->name,
                      LocalBinding{address, storage_type, parameter_type});
    }

    std::unordered_set<std::string> explicit_member_initializers;
    if (definition.children.size() > 3 && definition.children[3] &&
        definition.children[3]->kind ==
            frontend::AstKind::MemberInitializerList) {
        for (const auto& initializer : definition.children[3]->children) {
            if (initializer) {
                explicit_member_initializers.insert(initializer->text);
            }
        }
    }
    if (emitting_constructor && current_class_) {
        if (current_class_->base &&
            current_class_->base->kind == frontend::TypeKind::Record &&
            current_class_->base->record &&
            !explicit_member_initializers.contains(
                current_class_->base->record->tag)) {
            auto* base_constructor = select_constructor(
                current_class_->base->record->tag, 0);
            auto* self = lookup_local("this");
            if (base_constructor && self && self->type &&
                self->type->kind == ir::TypeKind::Pointer) {
                auto* self_value = builder_.create_load(
                    self->type, self->address, false, 0, "ctor.base.this");
                if (self_value->type !=
                    base_constructor->type->parameters[0]) {
                    self_value = builder_.create(
                        ir::Opcode::BitCast,
                        base_constructor->type->parameters[0],
                        {self_value}, "ctor.base.cast");
                }
                create_call_instruction(base_constructor, {self_value},
                                     nullptr, "ctor.base.default");
            }
        }
        for (const auto& member : current_class_->members) {
            if (explicit_member_initializers.contains(member.name)) {
                continue;
            }
            if (member.default_initializer &&
                !member.default_initializer->children.empty() &&
                member.default_initializer->children.front()) {
                auto address = emit_implicit_member_lvalue(member.name);
                if (!address.value || !address.type) {
                    continue;
                }
                emit_initializer(
                    *member.default_initializer->children.front(),
                    address.value, address.type);
                continue;
            }
            auto address = emit_implicit_member_lvalue(member.name);
            if (!address.value || !address.type ||
                address.type->kind != ir::TypeKind::Struct) {
                continue;
            }
            const auto record = ir_records_.find(address.type);
            if (record == ir_records_.end() || !record->second) {
                continue;
            }
            auto* constructor = select_constructor(record->second->tag, 0);
            if (!constructor) {
                continue;
            }
            auto* self = address.value;
            if (self->type != constructor->type->parameters[0]) {
                self = builder_.create(
                    ir::Opcode::BitCast,
                    constructor->type->parameters[0], {self},
                    "ctor.member.this");
            }
            create_call_instruction(constructor, {self}, nullptr,
                                 "ctor.member.default");
        }
    }

    if (definition.children.size() > 3 && definition.children[3] &&
        definition.children[3]->kind == frontend::AstKind::MemberInitializerList) {
        for (const auto& initializer : definition.children[3]->children) {
            if (!initializer || initializer->text.empty()) {
                continue;
            }
            bool delegated = false;
            for (auto* record = current_class_; record != nullptr;
                 record = record->base && record->base->record
                              ? record->base->record.get()
                              : nullptr) {
                if (record->tag != initializer->text) {
                    continue;
                }
                const frontend::AstNode* delegated_arguments =
                    initializer->children.empty() || !initializer->children.front()
                        ? nullptr
                        : initializer->children.front().get();
                static const std::vector<std::unique_ptr<frontend::AstNode>>
                    no_arguments;
                const auto& argument_nodes =
                    delegated_arguments ? delegated_arguments->children : no_arguments;
                auto* constructor =
                    select_constructor(record->tag, argument_nodes.size());
                auto* self = lookup_local("this");
                if (constructor && self) {
                    auto* self_value =
                        builder_.create_load(self->type, self->address, false, 0,
                                             "delegate.this");
                    if (self_value->type != constructor->type->parameters[0]) {
                        self_value = builder_.create(
                            ir::Opcode::BitCast, constructor->type->parameters[0],
                            {self_value}, "delegate.cast");
                    }
                    std::vector<ir::Value*> arguments{self_value};
                    for (std::size_t index = 0; index < argument_nodes.size(); ++index) {
                        if (!argument_nodes[index]) {
                            continue;
                        }
                        auto argument = emit_expression(*argument_nodes[index]);
                        if (!argument.value) {
                            break;
                        }
                        const auto parameter_index = index + 1;
                        if (parameter_index < constructor->type->parameters.size()) {
                            argument.value = promote(
                                argument.value, argument.type,
                                constructor->type->parameters[parameter_index]);
                        }
                        arguments.push_back(argument.value);
                    }
                    create_call_instruction(constructor, std::move(arguments), nullptr,
                                         "delegate.ctor");
                } else {
                    error(initializer->location,
                          "delegating constructor '" + initializer->text +
                              "' has no matching constructor");
                }
                delegated = true;
                break;
            }
            if (delegated) {
                continue;
            }
            auto address = emit_implicit_member_lvalue(initializer->text);
            if (!address.value || !address.type) {
                error(initializer->location,
                      "unknown member initializer '" + initializer->text + "'");
                continue;
            }
            const frontend::AstNode* argument_list =
                initializer->children.empty() || !initializer->children.front()
                    ? nullptr
                    : initializer->children.front().get();
            static const std::vector<std::unique_ptr<frontend::AstNode>>
                empty_arguments;
            const auto& argument_nodes =
                argument_list ? argument_list->children : empty_arguments;
            if (address.type->kind == ir::TypeKind::Struct) {
                const auto record = ir_records_.find(address.type);
                ir::Function* constructor = nullptr;
                if (record != ir_records_.end()) {
                    constructor =
                        select_constructor(record->second->tag, argument_nodes.size());
                }
                if (!constructor) {
                    emit_zero_initializer(address.value, address.type);
                    continue;
                }
                std::vector<ir::Value*> arguments{address.value};
                for (std::size_t index = 0; index < argument_nodes.size(); ++index) {
                    if (!argument_nodes[index]) {
                        continue;
                    }
                    auto argument = emit_expression(*argument_nodes[index]);
                    if (!argument.value) {
                        break;
                    }
                    const auto parameter_index = index + 1;
                    if (parameter_index < constructor->type->parameters.size()) {
                        argument.value = promote(
                            argument.value, argument.type,
                            constructor->type->parameters[parameter_index]);
                    }
                    arguments.push_back(argument.value);
                }
                create_call_instruction(constructor, std::move(arguments), nullptr,
                                     "member.ctor");
                continue;
            }
            if (argument_nodes.empty() || !argument_nodes.front()) {
                emit_zero_initializer(address.value, address.type);
                continue;
            }
            const frontend::RecordMember* member_info = nullptr;
            if (current_class_) {
                for (const auto& member : current_class_->members) {
                    if (member.name == initializer->text) {
                        member_info = &member;
                        break;
                    }
                }
            }
            if (member_info && member_info->type &&
                member_info->type->is_reference) {
                auto reference = emit_lvalue(*argument_nodes.front());
                if (!reference.value) {
                    continue;
                }
                auto* stored = reference.value;
                if (stored->type != address.type) {
                    stored = builder_.create(ir::Opcode::BitCast, address.type,
                                             {stored}, "member.reference");
                }
                builder_.create_store(stored, address.value);
                continue;
            }
            auto value = emit_expression(*argument_nodes.front());
            if (!value.value) {
                continue;
            }
            builder_.create_store(
                promote(value.value, value.type, address.type), address.value);
        }
    }

    labels_.clear();
    std::function<void(const frontend::AstNode&)> collect_labels =
        [&](const frontend::AstNode& node) {
        if (node.kind == frontend::AstKind::LabelStatement &&
            !node.text.empty()) {
            if (labels_.contains(node.text)) {
                error(node.location,
                      "duplicate label '" + node.text + "'");
            } else {
                labels_[node.text] =
                    function->add_block("label." + node.text);
            }
        }
        for (const auto& child : node.children) {
            if (child) {
                collect_labels(*child);
            }
        }
    };
    collect_labels(*body);

    emit_statement(*body);
    if (!builder_.insert_block()->terminated()) {
        emit_cleanups();
        if (function->type && function->type->return_type &&
            !ir::is_void(function->type->return_type)) {
            auto* return_value =
                module_->constant_undef(function->type->return_type);
            builder_.create_ret(return_value);
        } else {
            builder_.create_ret(nullptr);
        }
    }

    // Every pad is emitted now that the body's blocks exist; a pad's cleanups
    // are the objects that existed at the point it was created.
    emit_cleanup_pads();

    pop_scope();
    scopes_.clear();
    current_function_ = nullptr;
    current_class_ = previous_class;
    in_destructor_ = previous_destructor;
    in_constructor_ = previous_constructor;
    in_static_method_ = previous_static_method;
}

void CIRGen::emit_statement(const frontend::AstNode& statement) {
    if (statement.location.valid()) {
        const auto line = diagnostics_.source_manager().line_column(statement.location);
        ir::DebugLocation location;
        location.file = std::string(diagnostics_.source_manager().file_name(statement.location));
        location.line = line.line;
        location.column = line.column;
        builder_.set_debug_location(std::move(location));
    }
    switch (statement.kind) {
    case frontend::AstKind::CompoundStatement:
        emit_compound(statement);
        break;
    case frontend::AstKind::ThrowStatement:
        emit_throw(statement);
        break;
    case frontend::AstKind::TryStatement:
        emit_try(statement);
        break;
    case frontend::AstKind::Declaration:
        emit_declaration(statement);
        break;
    case frontend::AstKind::ExpressionStatement:
        if (!statement.children.empty() && statement.children.front()) {
            static_cast<void>(emit_expression(*statement.children.front()));
        }
        break;
    case frontend::AstKind::ReturnStatement:
        emit_return(statement);
        break;
    case frontend::AstKind::IfStatement: {
        if (statement.children.size() < 2 || !statement.children[0] ||
            !statement.children[1]) {
            error(statement.location, "malformed if statement");
            return;
        }
        if (std::find(statement.labels.begin(), statement.labels.end(), "constexpr") !=
            statement.labels.end()) {
            long long condition = 0;
            if (sema_.evaluate_constant_expression(*statement.children[0], condition)) {
                const auto branch = condition != 0 ? 1u : 2u;
                auto* end_block = current_function_->add_block("if.constexpr.end");
                if (branch < statement.children.size() && statement.children[branch]) {
                    emit_statement(*statement.children[branch]);
                }
                if (!builder_.insert_block()->terminated()) {
                    emit_branch(end_block);
                }
                builder_.set_insert_point(end_block);
                return;
            }
        }
        auto* condition = emit_condition(*statement.children[0]);
        auto* then_block = current_function_->add_block("if.then");
        auto* else_block = current_function_->add_block("if.else");
        auto* end_block = current_function_->add_block("if.end");
        emit_conditional_branch(condition, then_block, else_block);

        builder_.set_insert_point(then_block);
        emit_statement(*statement.children[1]);
        emit_branch(end_block);

        builder_.set_insert_point(else_block);
        if (statement.children.size() > 2 && statement.children[2]) {
            emit_statement(*statement.children[2]);
        }
        emit_branch(end_block);

        builder_.set_insert_point(end_block);
        break;
    }
    case frontend::AstKind::WhileStatement: {
        if (statement.children.size() < 2 || !statement.children[0] ||
            !statement.children[1]) {
            error(statement.location, "malformed while statement");
            return;
        }
        auto* condition_block = current_function_->add_block("while.cond");
        auto* body_block = current_function_->add_block("while.body");
        auto* end_block = current_function_->add_block("while.end");
        emit_branch(condition_block);
        builder_.set_insert_point(condition_block);
        auto* condition = emit_condition(*statement.children[0]);
        emit_conditional_branch(condition, body_block, end_block);
        builder_.set_insert_point(body_block);
        break_targets_.push_back({end_block, local_cleanups_.size()});
        continue_targets_.push_back({condition_block, local_cleanups_.size()});
        emit_statement(*statement.children[1]);
        break_targets_.pop_back();
        continue_targets_.pop_back();
        emit_branch(condition_block);
        builder_.set_insert_point(end_block);
        break;
    }
    case frontend::AstKind::DoStatement: {
        if (statement.children.size() < 2) {
            error(statement.location, "malformed do statement");
            return;
        }
        auto* body_block = current_function_->add_block("do.body");
        auto* condition_block = current_function_->add_block("do.cond");
        auto* end_block = current_function_->add_block("do.end");
        emit_branch(body_block);
        builder_.set_insert_point(body_block);
        break_targets_.push_back({end_block, local_cleanups_.size()});
        continue_targets_.push_back({condition_block, local_cleanups_.size()});
        emit_statement(*statement.children[0]);
        break_targets_.pop_back();
        continue_targets_.pop_back();
        emit_branch(condition_block);
        builder_.set_insert_point(condition_block);
        auto* condition = emit_condition(*statement.children[1]);
        emit_conditional_branch(condition, body_block, end_block);
        builder_.set_insert_point(end_block);
        break;
    }
    case frontend::AstKind::RangeForStatement: {
        if (statement.children.size() < 4 || !statement.children[2] ||
            !statement.children[3]) {
            error(statement.location, "malformed range-for statement");
            return;
        }
        push_scope();
        const auto element_type =
            sema_.declaration_type(*statement.children[0], statement.children[1].get());
        const bool bound_by_reference =
            element_type && element_type->is_reference;
        const auto element_ir = map_type(
            bound_by_reference ? element_type->element : element_type);
        std::uint64_t count = 0;
        Expression range;
        if (statement.children[2]->kind == frontend::AstKind::InitializerList) {
            count = statement.children[2]->children.size();
            if (count > 0 && element_ir) {
                auto* storage = builder_.create_alloca(
                    module_->types().array_type(element_ir, count), 0, "range.list");
                for (std::size_t index = 0; index < count; ++index) {
                    const auto& element = statement.children[2]->children[index];
                    if (!element) {
                        continue;
                    }
                    auto* zero = module_->constant_int(int_type(), 0);
                    auto* slot_index = module_->constant_int(
                        int_type(), static_cast<std::int64_t>(index));
                    auto* slot = builder_.create_gep(
                        module_->types().array_type(element_ir, count), storage,
                        {zero, slot_index}, false, "range.slot");
                    slot->type = module_->types().pointer_type(element_ir);
                    emit_initializer(*element, slot, element_ir);
                }
                auto* zero = module_->constant_int(int_type(), 0);
                auto* first = builder_.create_gep(
                    module_->types().array_type(element_ir, count), storage,
                    {zero, zero}, false, "range.first");
                first->type = module_->types().pointer_type(element_ir);
                range = {first, module_->types().pointer_type(element_ir)};
            }
        } else {
            range = emit_expression(*statement.children[2]);
            if (statement.children[2]->kind == frontend::AstKind::Identifier) {
                if (auto* local = lookup_local(statement.children[2]->text)) {
                    if (local->type && local->type->kind == ir::TypeKind::Array) {
                        count = local->type->count;
                    }
                }
            }
        }
        const auto value_name = declarator_name(*statement.children[1]);
        // A class with begin() and end() is iterated through them: the two
        // calls give a cursor and a limit, and the loop advances the cursor.
        if (statement.children[2]->kind == frontend::AstKind::Identifier) {
            if (auto* local = lookup_local(statement.children[2]->text)) {
                if (local->frontend_type &&
                    local->frontend_type->kind == frontend::TypeKind::Record &&
                    local->frontend_type->record) {
                    const auto methods =
                        class_methods_.find(local->frontend_type->record->tag);
                    if (methods != class_methods_.end()) {
                        const auto begin_method = methods->second.find("begin");
                        const auto end_method = methods->second.find("end");
                        if (begin_method != methods->second.end() &&
                            end_method != methods->second.end() &&
                            begin_method->second->type->return_type &&
                            begin_method->second->type->return_type->kind ==
                                ir::TypeKind::Pointer) {
                            if (emit_range_for_class(statement, *begin_method->second,
                                                     *end_method->second, value_name,
                                                     bound_by_reference, element_ir,
                                                     element_type)) {
                                pop_scope();
                                break;
                            }
                        }
                    }
                }
            }
        }
        if (!range.value || count == 0) {
            error(statement.location, "range-for requires a bounded array");
            pop_scope();
            return;
        }
        auto* value_address = builder_.create_alloca(
            bound_by_reference ? module_->types().pointer_type(element_ir)
                               : element_ir,
            0, value_name + ".range");
        declare_local(
            value_name,
            LocalBinding{value_address,
                         bound_by_reference
                             ? module_->types().pointer_type(element_ir)
                             : element_ir,
                         element_type});
        auto* index = builder_.create_alloca(int_type(), 0, "range.index");
        builder_.create_store(module_->constant_int(int_type(), 0), index);

        auto* condition_block = current_function_->add_block("range.cond");
        auto* body_block = current_function_->add_block("range.body");
        auto* increment_block = current_function_->add_block("range.inc");
        auto* end_block = current_function_->add_block("range.end");
        emit_branch(condition_block);
        builder_.set_insert_point(condition_block);
        auto* index_value = builder_.create_load(int_type(), index, false, 0, "range.i");
        auto* limit = module_->constant_int(int_type(), static_cast<std::int64_t>(count));
        auto* condition =
            builder_.create_icmp(ir::ICmpPredicate::Ult, index_value, limit, "range.cmp");
        emit_conditional_branch(condition, body_block, end_block);

        builder_.set_insert_point(body_block);
        auto* element = builder_.create_gep(element_ir, range.value, {index_value}, false,
                                            "range.element");
        element->type = module_->types().pointer_type(element_ir);
        if (bound_by_reference) {
            builder_.create_store(element, value_address);
        } else {
            auto* loaded =
                builder_.create_load(element_ir, element, false, 0, "range.load");
            builder_.create_store(loaded, value_address);
        }
        break_targets_.push_back({end_block, local_cleanups_.size()});
        continue_targets_.push_back({increment_block, local_cleanups_.size()});
        emit_statement(*statement.children[3]);
        break_targets_.pop_back();
        continue_targets_.pop_back();
        emit_branch(increment_block);

        builder_.set_insert_point(increment_block);
        auto* current = builder_.create_load(int_type(), index, false, 0, "range.old");
        auto* next = builder_.create_add(current, module_->constant_int(int_type(), 1),
                                         "range.next");
        builder_.create_store(next, index);
        emit_branch(condition_block);
        builder_.set_insert_point(end_block);
        pop_scope();
        break;
    }
    case frontend::AstKind::ForStatement: {
        if (statement.children.size() < 4) {
            error(statement.location, "malformed for statement");
            return;
        }
        push_scope();
        if (statement.children[0]) {
            if (statement.children[0]->kind == frontend::AstKind::Declaration) {
                emit_declaration(*statement.children[0]);
            } else {
                static_cast<void>(emit_expression(*statement.children[0]));
            }
        }
        auto* condition_block = current_function_->add_block("for.cond");
        auto* body_block = current_function_->add_block("for.body");
        auto* increment_block = current_function_->add_block("for.inc");
        auto* end_block = current_function_->add_block("for.end");
        emit_branch(condition_block);
        builder_.set_insert_point(condition_block);
        if (statement.children[1]) {
            auto* condition = emit_condition(*statement.children[1]);
            emit_conditional_branch(condition, body_block, end_block);
        } else {
            builder_.create_br(body_block);
        }
        builder_.set_insert_point(body_block);
        break_targets_.push_back({end_block, local_cleanups_.size()});
        continue_targets_.push_back({increment_block, local_cleanups_.size()});
        emit_statement(*statement.children[3]);
        break_targets_.pop_back();
        continue_targets_.pop_back();
        emit_branch(increment_block);
        builder_.set_insert_point(increment_block);
        if (statement.children[2]) {
            static_cast<void>(emit_expression(*statement.children[2]));
        }
        emit_branch(condition_block);
        builder_.set_insert_point(end_block);
        pop_scope();
        break;
    }
    case frontend::AstKind::BreakStatement:
        if (break_targets_.empty()) {
            error(statement.location, "break outside a loop");
        } else {
            emit_cleanups_for_exit(break_targets_.back().mark);
            emit_branch(break_targets_.back().block);
        }
        break;
    case frontend::AstKind::ContinueStatement:
        if (continue_targets_.empty()) {
            error(statement.location, "continue outside a loop");
        } else {
            emit_cleanups_for_exit(continue_targets_.back().mark);
            emit_branch(continue_targets_.back().block);
        }
        break;
    case frontend::AstKind::NullStatement:
        break;
    case frontend::AstKind::StaticAssertDeclaration:
        break;
    case frontend::AstKind::SwitchStatement: {
        if (statement.children.size() < 2 || !statement.children[0] ||
            !statement.children[1]) {
            error(statement.location, "malformed switch statement");
            break;
        }
        auto condition = emit_expression(*statement.children[0]);
        if (condition.value && condition.type &&
            condition.type->kind == ir::TypeKind::Struct &&
            statement.children[0]->kind ==
                frontend::AstKind::CallExpression) {
            auto* storage = builder_.create_alloca(condition.type, 0,
                                                       "switch.temp");
            builder_.create_store(condition.value, storage);
            const auto record = ir_records_.find(condition.type);
            if (record != ir_records_.end() && record->second) {
                const auto methods =
                    class_methods_.find(record->second->tag);
                if (methods != class_methods_.end()) {
                    auto method = methods->second.find("operator int");
                    if (method == methods->second.end()) {
                        method = methods->second.find("operator bool");
                    }
                    if (method != methods->second.end()) {
                        auto* self = storage;
                        if (self->type !=
                            method->second->type->parameters[0]) {
                            self = builder_.create(
                                ir::Opcode::BitCast,
                                method->second->type->parameters[0],
                                {self}, "switch.this");
                        }
                        auto* call = create_call_instruction(
                            method->second, {self},
                            method->second->type->return_type,
                            "switch.conversion");
                        condition.value = call;
                        condition.type =
                            method->second->type->return_type;
                    }
                }
            }
        }
        if (!condition.value || !ir::is_integer(condition.type)) {
            error(statement.location, "switch condition must be an integer");
            break;
        }
        const auto* body = statement.children[1].get();
        if (!body ||
            body->kind != frontend::AstKind::CompoundStatement) {
            error(statement.location, "switch body must be a block");
            break;
        }
        auto* end_block = current_function_->add_block("switch.end");
        std::vector<std::pair<const frontend::AstNode*, ir::BasicBlock*>>
            case_entries;
        ir::BasicBlock* default_block = end_block;
        for (const auto& child : body->children) {
            if (!child) {
                continue;
            }
            if (child->kind == frontend::AstKind::CaseStatement) {
                case_entries.emplace_back(
                    child.get(),
                    current_function_->add_block("switch.case"));
            } else if (child->kind ==
                       frontend::AstKind::DefaultStatement) {
                default_block =
                    current_function_->add_block("switch.default");
                case_entries.emplace_back(child.get(), default_block);
            }
        }
        std::vector<std::pair<ir::ConstantInt*, ir::BasicBlock*>>
            switch_cases;
        for (const auto& [node, block] : case_entries) {
            if (node->kind != frontend::AstKind::CaseStatement ||
                node->children.empty() || !node->children.front()) {
                continue;
            }
            long long value = 0;
            if (!sema_.evaluate_constant_expression(
                    *node->children.front(), value)) {
                error(node->location,
                      "case label is not an integer constant");
                continue;
            }
            switch_cases.push_back(
                {module_->constant_int(condition.type, value), block});
        }
        for (const auto& [value, block] : switch_cases) {
            auto* comparison = builder_.create_icmp(
                ir::ICmpPredicate::Eq, condition.value, value,
                "switch.compare");
            auto* next = current_function_->add_block("switch.check");
            emit_conditional_branch(comparison, block, next);
            builder_.set_insert_point(next);
        }
        emit_branch(default_block);
        push_scope();
        const auto mark = local_cleanups_.size();
        cleanup_marks_.push_back(mark);
        break_targets_.push_back({end_block, local_cleanups_.size()});
        for (const auto& child : body->children) {
            if (!child) {
                continue;
            }
            const auto entry = std::find_if(
                case_entries.begin(), case_entries.end(),
                [&](const auto& candidate) {
                    return candidate.first == child.get();
                });
            if (entry != case_entries.end()) {
                if (!builder_.insert_block()->terminated() &&
                    builder_.insert_block() != entry->second) {
                    emit_branch(entry->second);
                }
                builder_.set_insert_point(entry->second);
                if (child->kind == frontend::AstKind::CaseStatement) {
                    if (child->children.size() > 1 &&
                        child->children[1]) {
                        emit_statement(*child->children[1]);
                    }
                } else if (!child->children.empty() &&
                           child->children.front()) {
                    emit_statement(*child->children.front());
                }
            } else if (!builder_.insert_block()->terminated()) {
                emit_statement(*child);
            }
        }
        emit_cleanups_to(mark);
        cleanup_marks_.pop_back();
        pop_scope();
        if (!builder_.insert_block()->terminated()) {
            emit_branch(end_block);
        }
        builder_.set_insert_point(end_block);
        break_targets_.pop_back();
        break;
    }
    case frontend::AstKind::GotoStatement: {
        if (!local_cleanups_.empty()) {
            error(statement.location,
                  "goto across an active object scope is not supported");
            break;
        }
        const auto found = labels_.find(statement.text);
        if (found == labels_.end()) {
            error(statement.location,
                  "goto to undefined label '" + statement.text + "'");
            break;
        }
        if (!builder_.insert_block()->terminated()) {
            emit_branch(found->second);
        }
        break;
    }
    case frontend::AstKind::LabelStatement: {
        const auto found = labels_.find(statement.text);
        if (found == labels_.end()) {
            break;
        }
        if (builder_.insert_block() != found->second &&
            !builder_.insert_block()->terminated()) {
            emit_branch(found->second);
        }
        builder_.set_insert_point(found->second);
        for (const auto& child : statement.children) {
            if (child) {
                emit_statement(*child);
            }
        }
        break;
    }
    case frontend::AstKind::CaseStatement:
    case frontend::AstKind::DefaultStatement:
        error(statement.location, "statement kind is not implemented in IRGen yet");
        break;
    default:
        error(statement.location, "unsupported statement in IRGen");
        break;
    }
}

void CIRGen::emit_copy(ir::Value* destination, ir::Value* source, ir::TypePtr type) {
    if (!destination || !source || !type) {
        return;
    }
    if (type->kind == ir::TypeKind::Struct) {
        for (std::size_t index = 0; index < type->fields.size(); ++index) {
            auto* zero = module_->constant_int(int_type(), 0);
            auto* member_index =
                module_->constant_int(int_type(), static_cast<std::int64_t>(index));
            auto* destination_member =
                builder_.create_gep(type, destination, {zero, member_index}, false,
                                    "copy.dst");
            destination_member->type = module_->types().pointer_type(type->fields[index]);
            auto* source_member =
                builder_.create_gep(type, source, {zero, member_index}, false, "copy.src");
            source_member->type = module_->types().pointer_type(type->fields[index]);
            emit_copy(destination_member, source_member, type->fields[index]);
        }
        return;
    }
    if (type->kind == ir::TypeKind::Array) {
        for (std::size_t index = 0; index < type->count; ++index) {
            auto* zero = module_->constant_int(int_type(), 0);
            auto* member_index =
                module_->constant_int(int_type(), static_cast<std::int64_t>(index));
            auto* destination_member =
                builder_.create_gep(type, destination, {zero, member_index}, false,
                                    "copy.dst");
            destination_member->type = module_->types().pointer_type(type->element);
            auto* source_member =
                builder_.create_gep(type, source, {zero, member_index}, false, "copy.src");
            source_member->type = module_->types().pointer_type(type->element);
            emit_copy(destination_member, source_member, type->element);
        }
        return;
    }
    auto* value = builder_.create_load(type, source, false, 0, "copy.load");
    builder_.create_store(value, destination);
}

void CIRGen::emit_cleanups_to(std::size_t mark) {
    std::vector<LocalCleanup> cleanups;
    while (local_cleanups_.size() > mark) {
        cleanups.push_back(local_cleanups_.back());
        local_cleanups_.pop_back();
    }
    emit_cleanup_sequence(cleanups);
}

void CIRGen::emit_cleanups_for_exit(std::size_t mark) {
    std::vector<LocalCleanup> cleanups;
    for (std::size_t index = local_cleanups_.size(); index > mark; --index) {
        cleanups.push_back(local_cleanups_[index - 1]);
    }
    emit_cleanup_sequence(cleanups);
}

void CIRGen::emit_cleanup_sequence(const std::vector<LocalCleanup>& cleanups) {
    for (const auto& cleanup : cleanups) {
        if (!cleanup.address || !cleanup.destructor ||
            cleanup.destructor->type->parameters.empty()) {
            continue;
        }
        if (cleanup.count <= 1 || !cleanup.element) {
            auto* address = cleanup.address;
            if (address->type != cleanup.destructor->type->parameters[0]) {
                address = builder_.create(
                    ir::Opcode::BitCast,
                    cleanup.destructor->type->parameters[0], {address},
                    "dtor.this");
            }
            create_call_instruction(cleanup.destructor, {address}, nullptr,
                                 "dtor.call");
            continue;
        }
        auto* index_slot = builder_.create_alloca(int_type(), 0, "dtor.index");
        builder_.create_store(module_->constant_int(int_type(), 0), index_slot);
        auto* condition = current_function_->add_block("dtor.cond");
        auto* body = current_function_->add_block("dtor.body");
        auto* done = current_function_->add_block("dtor.done");
        emit_branch(condition);
        builder_.set_insert_point(condition);
        auto* index = builder_.create_load(int_type(), index_slot, false, 0,
                                           "dtor.index.load");
        auto* limit = module_->constant_int(
            int_type(), static_cast<std::int64_t>(cleanup.count));
        auto* compare = builder_.create_icmp(ir::ICmpPredicate::Ult, index,
                                             limit, "dtor.cmp");
        emit_conditional_branch(compare, body, done);
        builder_.set_insert_point(body);
        auto* zero = module_->constant_int(int_type(), 0);
        auto* element = builder_.create_gep(
            cleanup.address->type->element, cleanup.address, {zero, index},
            false, "dtor.element");
        element->type = module_->types().pointer_type(cleanup.element);
        auto* address = element;
        if (address->type != cleanup.destructor->type->parameters[0]) {
            address = builder_.create(
                ir::Opcode::BitCast, cleanup.destructor->type->parameters[0],
                {address}, "dtor.this");
        }
        create_call_instruction(cleanup.destructor, {address}, nullptr,
                             "dtor.call");
        auto* next = builder_.create_add(
            index, module_->constant_int(int_type(), 1), "dtor.next");
        builder_.create_store(next, index_slot);
        emit_branch(condition);
        builder_.set_insert_point(done);
    }
}

void CIRGen::emit_cleanups() {
    emit_cleanups_to(0);
}

void CIRGen::emit_compound(const frontend::AstNode& compound) {
    push_scope();
    const auto mark = local_cleanups_.size();
    cleanup_marks_.push_back(mark);
    scope_pads_.push_back({mark, owned_landing_pad(current_landing_pad_)});
    std::function<bool(const frontend::AstNode&)> contains_label =
        [&](const frontend::AstNode& node) {
        if (node.kind == frontend::AstKind::LabelStatement) {
            return true;
        }
        for (const auto& child : node.children) {
            if (child && contains_label(*child)) {
                return true;
            }
        }
        return false;
    };
    for (const auto& child : compound.children) {
        if (child) {
            if (builder_.insert_block()->terminated() &&
                !contains_label(*child)) {
                continue;
            }
            emit_statement(*child);
        }
    }
    auto& scope = scope_pads_.back();
    // Destructor calls on the normal path are protected by the enclosing pad:
    // this scope's own pad exists for unwinding into, not for the exit path.
    current_landing_pad_ = scope.enclosing;
    if (builder_.insert_block()->terminated()) {
        local_cleanups_.resize(mark);
    } else {
        emit_cleanups_to(mark);
    }
    cleanup_marks_.pop_back();
    scope_pads_.pop_back();
    pop_scope();
}

ir::BasicBlock* CIRGen::owned_landing_pad(ir::BasicBlock* pad) const {
    return pad && current_function_ && pad->parent == current_function_ ? pad
                                                                        : nullptr;
}

void CIRGen::note_scope_cleanup() {
    if (scope_pads_.empty() || !current_function_) {
        return;
    }
    // A target without exceptions has nothing to unwind into, so a cleanup pad
    // would only turn ordinary calls into invokes the backend cannot lower. The
    // destructors still run on every normal exit, through
    // emit_cleanups_for_exit.
    if (!profile_.has_eh) {
        return;
    }
    // A pad already in force that covers every live cleanup needs no successor:
    // creating one would leave the call unwinding through an empty pad that
    // resumes into the real pad.
    for (const auto& existing : cleanup_pads_) {
        if (existing.block == current_landing_pad_ &&
            existing.covered >= local_cleanups_.size()) {
            return;
        }
    }
    // The pad covers the objects that exist now, not the ones the scope will
    // declare later: a construction point can unwind before they exist.
    CleanupPad pad;
    pad.block = current_function_->add_block("cleanup.pad");
    pad.enclosing = owned_landing_pad(current_landing_pad_);
    // The enclosing chain already destroys what it covers, so this pad takes the
    // rest; without that, one object would be destroyed twice.
    std::size_t base = 0;
    for (const auto& existing : cleanup_pads_) {
        if (existing.block == pad.enclosing) {
            base = existing.covered;
            break;
        }
    }
    for (std::size_t index = local_cleanups_.size(); index > base; --index) {
        pad.cleanups.push_back(local_cleanups_[index - 1]);
    }
    pad.covered = local_cleanups_.size();
    current_landing_pad_ = pad.block;
    cleanup_pads_.push_back(std::move(pad));
}

void CIRGen::emit_cleanup_pads() {
    if (!current_function_) {
        cleanup_pads_.clear();
        return;
    }
    // A pad that no terminator unwinds into is dead: it has no predecessors, so
    // nothing in the function dominates it and its destructor calls would use
    // values from the entry block. It still needs a terminator.
    std::unordered_set<const ir::BasicBlock*> targeted;
    for (const auto& block : current_function_->blocks) {
        const auto* terminator = block->terminator();
        if (!terminator) {
            continue;
        }
        for (const auto* successor : terminator->successors) {
            if (successor) {
                targeted.insert(successor);
            }
        }
        if (terminator->default_successor) {
            targeted.insert(terminator->default_successor);
        }
    }
    for (auto& pad : cleanup_pads_) {
        if (!pad.block) {
            continue;
        }
        if (targeted.count(pad.block) == 0) {
            auto* previous_block = builder_.insert_block();
            builder_.set_insert_point(pad.block);
            builder_.create_unreachable();
            builder_.set_insert_point(previous_block);
            continue;
        }
        auto* previous_block = builder_.insert_block();
        builder_.set_insert_point(pad.block);
        auto* landing = builder_.create_landingpad("cleanup.exception");
        const auto byte_type = module_->types().int_type(8);
        const auto byte_pointer = module_->types().pointer_type(byte_type);
        auto* object_slot = builder_.create_gep(
            byte_type, landing, {module_->constant_int(int_type(), 0)}, false,
            "cleanup.object.slot");
        object_slot->type = module_->types().pointer_type(byte_pointer);
        auto* exception = builder_.create_load(byte_pointer, object_slot, false, 0,
                                               "cleanup.exception");
        current_landing_pad_ = pad.enclosing;
        emit_cleanup_sequence(pad.cleanups);
        emit_resume(exception);
        builder_.set_insert_point(previous_block);
    }
    cleanup_pads_.clear();
}

void CIRGen::emit_resume(ir::Value* exception) {
    const auto void_type = module_->types().void_type();
    if (!owned_landing_pad(current_landing_pad_)) {
        builder_.create_resume(exception);
        return;
    }
    const auto byte_pointer = module_->types().pointer_type(module_->types().int_type(8));
    auto* resume = ensure_runtime_function("_Unwind_Resume", void_type, {byte_pointer});
    create_call_instruction(resume, {exception}, void_type, "cleanup.resume");
    builder_.create_unreachable();
}

void CIRGen::emit_declaration(const frontend::AstNode& declaration) {
    if (declaration.children.empty() || !declaration.children.front()) {
        return;
    }
    const auto& specifiers = *declaration.children.front();
    if (has_label(specifiers, "typedef")) {
        return;
    }
    if (has_label(specifiers, "static") ||
        has_label(specifiers, "thread_local") ||
        has_label(specifiers, "_Thread_local")) {
        // A block-scope thread-local object has static storage on this
        // single-threaded VM, so it takes the same path as a local static and
        // gets a guarded initializer when its initializer is not constant.
        for (std::size_t index = 1; index < declaration.children.size();
             ++index) {
            const auto* init = declaration.children[index].get();
            if (!init || init->kind != frontend::AstKind::InitDeclarator ||
                init->children.empty() || !init->children.front()) {
                continue;
            }
            const auto* declarator = init->children.front().get();
            const auto name = declarator_name(*declarator);
            if (name.empty()) {
                continue;
            }
            auto frontend_type =
                sema_.declaration_type(specifiers, declarator);
            if (!frontend_type) {
                continue;
            }
            if (frontend_type->kind == frontend::TypeKind::Array &&
                !frontend_type->array_has_count &&
                init->children.size() > 1 && init->children[1]) {
                if (init->children[1]->kind ==
                    frontend::AstKind::InitializerList) {
                    auto inferred =
                        std::make_shared<frontend::Type>(*frontend_type);
                    inferred->array_count =
                        init->children[1]->children.size();
                    inferred->array_has_count = true;
                    frontend_type = inferred;
                } else if (init->children[1]->kind ==
                           frontend::AstKind::StringLiteral) {
                    const auto bytes =
                        decode_string(init->children[1]->text);
                    auto inferred =
                        std::make_shared<frontend::Type>(*frontend_type);
                    inferred->array_count = bytes.size() + 1;
                    inferred->array_has_count = true;
                    frontend_type = inferred;
                }
            }
            const auto type = map_type(frontend_type);
            if (!type) {
                continue;
            }
            const auto id = static_local_counter_++;
            auto* global = module_->add_global(
                "__static_" + std::to_string(id) + "_" + name, type, false, 0);
            bool initialized_at_runtime = false;
            const bool aggregate_initializer =
                init->children.size() > 1 && init->children[1] &&
                ((type->kind == ir::TypeKind::Array &&
                  (init->children[1]->kind ==
                       frontend::AstKind::InitializerList ||
                   init->children[1]->kind ==
                       frontend::AstKind::StringLiteral)) ||
                 (type->kind == ir::TypeKind::Struct &&
                  init->children[1]->kind ==
                      frontend::AstKind::InitializerList &&
                  frontend_type->record &&
                  !select_constructor(frontend_type->record->tag, 0)));
            if (aggregate_initializer) {
                auto* guard = module_->add_global(
                    "__static_guard_" + std::to_string(id),
                    module_->types().int_type(8), false, 0);
                guard->initializer =
                    module_->constant_int(module_->types().int_type(8), 0);
                auto* guard_value = builder_.create_load(
                    module_->types().int_type(8), guard, false, 0,
                    "static.guard");
                auto* zero =
                    module_->constant_int(module_->types().int_type(8), 0);
                auto* condition = builder_.create_icmp(
                    ir::ICmpPredicate::Eq, guard_value, zero, "static.cmp");
                auto* init_block = current_function_->add_block(
                    "static.init." + std::to_string(id));
                auto* end_block = current_function_->add_block(
                    "static.end." + std::to_string(id));
                emit_conditional_branch(condition, init_block, end_block);
                builder_.set_insert_point(init_block);
                builder_.create_store(
                    module_->constant_int(module_->types().int_type(8), 1),
                    guard);
                emit_initializer(*init->children[1], global, type);
                emit_branch(end_block);
                builder_.set_insert_point(end_block);
            }
            if (init->children.size() > 1 && init->children[1]) {
                long long constant = 0;
                if ((ir::is_integer(type) || ir::is_pointer(type)) &&
                    sema_.evaluate_constant_expression(*init->children[1],
                                                       constant)) {
                    global->initializer =
                        module_->constant_int(type, constant);
                } else if (ir::is_integer(type) || ir::is_pointer(type)) {
                    initialized_at_runtime = true;
                    auto* guard = module_->add_global(
                        "__static_guard_" + std::to_string(id), module_->types().int_type(8),
                        false, 0);
                    guard->initializer =
                        module_->constant_int(module_->types().int_type(8), 0);
                    auto* guard_value = builder_.create_load(
                        module_->types().int_type(8), guard, false, 0, "static.guard");
                    auto* zero = module_->constant_int(module_->types().int_type(8), 0);
                    auto* condition = builder_.create_icmp(
                        ir::ICmpPredicate::Eq, guard_value, zero,
                        "static.cmp");
                    auto* init_block = current_function_->add_block(
                        "static.init." + std::to_string(id));
                    auto* end_block = current_function_->add_block(
                        "static.end." + std::to_string(id));
                    emit_conditional_branch(condition, init_block, end_block);
                    builder_.set_insert_point(init_block);
                    builder_.create_store(
                        module_->constant_int(module_->types().int_type(8), 1), guard);
                    auto value = emit_expression(*init->children[1]);
                    if (value.value) {
                        builder_.create_store(
                            promote(value.value, value.type, type), global);
                    }
                    emit_branch(end_block);
                    builder_.set_insert_point(end_block);
                }
            }
            static_cast<void>(initialized_at_runtime);
            if (type->kind == ir::TypeKind::Struct) {
                const auto record = ir_records_.find(type);
                if (record != ir_records_.end() && record->second) {
                    std::vector<const frontend::AstNode*> argument_nodes;
                    if (init->children.size() > 1 && init->children[1] &&
                        init->children[1]->kind ==
                            frontend::AstKind::ExpressionStatement) {
                        for (const auto& argument :
                             init->children[1]->children) {
                            argument_nodes.push_back(argument.get());
                        }
                    }
                    auto* constructor = select_constructor(
                        record->second->tag, argument_nodes.size());
                    if (!constructor) {
                        constructor =
                            select_constructor(record->second->tag, 0);
                    }
                    if (constructor) {
                        auto* guard = module_->add_global(
                            "__static_guard_" + std::to_string(id),
                            module_->types().int_type(8), false, 0);
                        guard->initializer = module_->constant_int(
                            module_->types().int_type(8), 0);
                        auto* guard_value = builder_.create_load(
                            module_->types().int_type(8), guard, false, 0,
                            "static.guard");
                        auto* zero = module_->constant_int(
                            module_->types().int_type(8), 0);
                        auto* condition = builder_.create_icmp(
                            ir::ICmpPredicate::Eq, guard_value, zero,
                            "static.cmp");
                        auto* init_block = current_function_->add_block(
                            "static.init." + std::to_string(id));
                        auto* end_block = current_function_->add_block(
                            "static.end." + std::to_string(id));
                        emit_conditional_branch(condition, init_block,
                                                end_block);
                        builder_.set_insert_point(init_block);
                        builder_.create_store(
                            module_->constant_int(
                                module_->types().int_type(8), 1),
                            guard);
                        auto* self = static_cast<ir::Value*>(global);
                        if (self->type != constructor->type->parameters[0]) {
                            self = builder_.create(
                                ir::Opcode::BitCast,
                                constructor->type->parameters[0], {self},
                                "static.this");
                        }
                        std::vector<ir::Value*> arguments{self};
                        for (std::size_t argument_index = 0;
                             argument_index < argument_nodes.size();
                             ++argument_index) {
                            if (!argument_nodes[argument_index]) {
                                continue;
                            }
                            auto argument = emit_expression(
                                *argument_nodes[argument_index]);
                            if (!argument.value) {
                                continue;
                            }
                            const auto parameter_index = argument_index + 1;
                            if (parameter_index <
                                constructor->type->parameters.size()) {
                                argument.value = promote(
                                    argument.value, argument.type,
                                    constructor->type
                                        ->parameters[parameter_index]);
                            }
                            arguments.push_back(argument.value);
                        }
                        create_call_instruction(constructor,
                                             std::move(arguments), nullptr,
                                             "static.ctor");                        emit_branch(end_block);
                        builder_.set_insert_point(end_block);
                    }
                }
            }
            if (type->kind == ir::TypeKind::Struct) {
                const auto record = ir_records_.find(type);
                if (record != ir_records_.end() && record->second) {
                    for (auto* current = record->second; current != nullptr;
                         current = current->base && current->base->record
                                       ? current->base->record.get()
                                       : nullptr) {
                        const auto methods =
                            class_methods_.find(current->tag);
                        if (methods != class_methods_.end() &&
                            methods->second.find("~" + current->tag) !=
                                methods->second.end()) {
                            local_static_dtors_.push_back(
                                {global, record->second});
                            break;
                        }
                    }
                }
            }
            if (type->kind == ir::TypeKind::Array && type->element &&
                type->element->kind == ir::TypeKind::Struct &&
                type->count > 0) {
                const auto record = ir_records_.find(type->element);
                if (record != ir_records_.end() && record->second) {
                    auto* constructor =
                        select_constructor(record->second->tag, 0);
                    if (constructor) {
                        auto* guard = module_->add_global(
                            "__static_array_guard_" + std::to_string(id),
                            module_->types().int_type(8), false, 0);
                        guard->initializer = module_->constant_int(
                            module_->types().int_type(8), 0);
                        auto* guard_value = builder_.create_load(
                            module_->types().int_type(8), guard, false, 0,
                            "static.array.guard");
                        auto* zero = module_->constant_int(
                            module_->types().int_type(8), 0);
                        auto* condition = builder_.create_icmp(
                            ir::ICmpPredicate::Eq, guard_value, zero,
                            "static.array.cmp");
                        auto* init_block = current_function_->add_block(
                            "static.array.init." + std::to_string(id));
                        auto* end_block = current_function_->add_block(
                            "static.array.end." + std::to_string(id));
                        emit_conditional_branch(condition, init_block,
                                                end_block);
                        builder_.set_insert_point(init_block);
                        builder_.create_store(
                            module_->constant_int(
                                module_->types().int_type(8), 1),
                            guard);
                        for (std::uint64_t element_position = 0;
                             element_position < type->count;
                             ++element_position) {
                            auto* element_index = module_->constant_int(
                                int_type(),
                                static_cast<std::int64_t>(element_position));
                            auto* element = builder_.create_gep(
                                type, global, {zero, element_index}, false,
                                "static.array.element");
                            element->type =
                                module_->types().pointer_type(type->element);
                            create_call_instruction(constructor, {element},
                                                 nullptr, "static.array.ctor");
                        }
                        emit_branch(end_block);
                        builder_.set_insert_point(end_block);
                    }
                }
            }
            declare_local(name, LocalBinding{global, type, frontend_type});
        }
        return;
    }
    if (has_label(specifiers, "extern")) {
        error(declaration.location,
              "local extern variables are not implemented yet");
        return;
    }
    if (declaration.children.size() > 1 && declaration.children[1] &&
        declaration.children[1]->kind == frontend::AstKind::StructuredBinding) {
        const auto& binding = *declaration.children[1];
        if (binding.children.empty()) {
            return;
        }
        const auto* initializer = binding.children.back().get();
        auto source = initializer ? emit_lvalue(*initializer) : Expression{};
        if (!source.value || !source.type ||
            (source.type->kind != ir::TypeKind::Struct &&
             source.type->kind != ir::TypeKind::Array)) {
            error(declaration.location, "structured binding requires a struct initializer");
            return;
        }
        if (source.type->kind == ir::TypeKind::Array) {
            for (std::size_t index = 0; index + 1 < binding.children.size(); ++index) {
                const auto* name_node = binding.children[index].get();
                if (!name_node || !source.type->element ||
                    index >= source.type->count) {
                    continue;
                }
                auto* zero = module_->constant_int(int_type(), 0);
                auto* element_index = module_->constant_int(
                    int_type(), static_cast<std::int64_t>(index));
                auto* element = builder_.create_gep(
                    source.type, source.value, {zero, element_index}, false,
                    "binding.element");
                element->type =
                    module_->types().pointer_type(source.type->element);
                auto* storage = builder_.create_alloca(
                    source.type->element, 0, name_node->text + ".binding");
                builder_.create_store(
                    builder_.create_load(source.type->element, element, false, 0,
                                         "binding.load"),
                    storage);
                declare_local(name_node->text,
                              LocalBinding{storage, source.type->element, nullptr});
            }
            return;
        }
        const frontend::TypePtr frontend_type =
            initializer && initializer->kind == frontend::AstKind::Identifier
                ? (lookup_local(initializer->text)
                       ? lookup_local(initializer->text)->frontend_type
                       : nullptr)
                : nullptr;
        const frontend::RecordInfo* record = nullptr;
        if (frontend_type && frontend_type->kind == frontend::TypeKind::Record) {
            record = frontend_type->record.get();
        }
        for (std::size_t index = 0; index + 1 < binding.children.size(); ++index) {
            const auto* name_node = binding.children[index].get();
            if (!name_node || index >= source.type->fields.size()) {
                continue;
            }
            auto* zero = module_->constant_int(int_type(), 0);
            auto* field_index =
                module_->constant_int(int_type(), static_cast<std::int64_t>(index));
            auto* field = builder_.create_gep(source.type, source.value,
                                              {zero, field_index}, false,
                                              name_node->text + ".binding");
            field->type = module_->types().pointer_type(source.type->fields[index]);
            frontend::TypePtr field_frontend;
            if (record && index < record->members.size()) {
                field_frontend = record->members[index].type;
            }
            declare_local(name_node->text,
                          LocalBinding{field, source.type->fields[index], field_frontend});
        }
        return;
    }
    for (std::size_t index = 1; index < declaration.children.size(); ++index) {
        const auto* init = declaration.children[index].get();
        if (!init || init->kind != frontend::AstKind::InitDeclarator ||
            init->children.empty()) {
            continue;
        }
        const auto* declarator = init->children.front().get();
        if (!declarator) {
            continue;
        }
        auto frontend_type = sema_.declaration_type(specifiers, declarator);
        if (has_label(specifiers, "auto") && init->children.size() > 1 &&
            init->children[1]) {
            bool inferred_from_local = false;
            if (init->children[1]->kind == frontend::AstKind::Identifier) {
                if (auto* local = lookup_local(init->children[1]->text)) {
                    frontend_type = local->frontend_type;
                    inferred_from_local = true;
                }
            }
            if (!inferred_from_local) {
                frontend_type = sema_.expression_type(*init->children[1]);
            }
        }
        std::function<frontend::TypePtr(const frontend::AstNode&)>
            infer_decltype;
        infer_decltype = [&](const frontend::AstNode& node)
            -> frontend::TypePtr {
            switch (node.kind) {
            case frontend::AstKind::Identifier:
                if (auto* local = lookup_local(node.text)) {
                    return local->frontend_type;
                }
                return nullptr;
            case frontend::AstKind::IntegerLiteral:
                return sema_.type_named("int");
            case frontend::AstKind::FloatLiteral:
                return sema_.type_named("double");
            case frontend::AstKind::CastExpression:
                return node.children.empty() || !node.children.front()
                           ? nullptr
                           : sema_.type_from_type_name(
                                 *node.children.front());
            case frontend::AstKind::BinaryExpression: {
                if (node.children.size() < 2) {
                    return nullptr;
                }
                const auto left = infer_decltype(*node.children[0]);
                const auto right = infer_decltype(*node.children[1]);
                if ((left && left->kind == frontend::TypeKind::Double) ||
                    (right && right->kind == frontend::TypeKind::Double)) {
                    return sema_.type_named("double");
                }
                if ((left && left->kind == frontend::TypeKind::Float) ||
                    (right && right->kind == frontend::TypeKind::Float)) {
                    return sema_.type_named("float");
                }
                return left ? left : right;
            }
            default:
                return nullptr;
            }
        };
        for (const auto& specifier : specifiers.children) {
            if (specifier && specifier->kind == frontend::AstKind::TypeName &&
                specifier->text == "decltype" &&
                !specifier->children.empty() &&
                specifier->children.front()) {
                if (const auto inferred =
                        infer_decltype(*specifier->children.front())) {
                    frontend_type = inferred;
                }
            }
        }
        if (!frontend_type) {
            continue;
        }
        if (frontend_type->kind == frontend::TypeKind::Array &&
            !frontend_type->array_has_count && init->children.size() > 1 &&
            init->children[1] &&
            init->children[1]->kind == frontend::AstKind::StringLiteral) {
            const auto bytes = decode_string(init->children[1]->text);
            auto inferred = std::make_shared<frontend::Type>(*frontend_type);
            inferred->array_count = bytes.size() + 1;
            inferred->array_has_count = true;
            frontend_type = inferred;
        }
        const auto type = map_type(frontend_type);
        const auto name = declarator_name(*declarator);
        if (name.empty()) {
            error(declarator->location, "local declaration has no name");
            continue;
        }
        if (init->children.size() > 1 && init->children[1] &&
            init->children[1]->kind == frontend::AstKind::LambdaExpression &&
            (!init->children[1]->labels.empty() ||
             init->children[1]->children.size() > 2)) {
            auto value = emit_lambda(*init->children[1]);
            if (!value.value) {
                continue;
            }
            auto* storage = builder_.create_alloca(value.type, 0, name + ".closure");
            builder_.create_store(value.value, storage);
            declare_local(name, LocalBinding{storage, value.type, frontend_type});
            const auto metadata = lambda_metadata_.find(init->children[1].get());
            if (metadata != lambda_metadata_.end()) {
                lambda_locals_[name] = metadata->second;
            }
            continue;
        }
        auto* address =
            builder_.create_alloca(type, alignment_of(specifiers, frontend_type),
                                   name + ".addr");
        declare_local(name, LocalBinding{address, type, frontend_type});
        if (type && type->kind == ir::TypeKind::Array &&
            type->element && type->element->kind == ir::TypeKind::Struct &&
            type->count > 0 && init->children.size() <= 1) {
            const auto record = ir_records_.find(type->element);
            if (record != ir_records_.end() && record->second) {
                auto* constructor =
                    select_constructor(record->second->tag, 0, 0);
                if (constructor) {
                    for (std::uint64_t element_position = 0;
                         element_position < type->count;
                         ++element_position) {
                        auto* zero = module_->constant_int(int_type(), 0);
                        auto* element_index = module_->constant_int(
                            int_type(),
                            static_cast<std::int64_t>(element_position));
                        auto* element = builder_.create_gep(
                            type, address, {zero, element_index}, false,
                            "array.element");
                        element->type =
                            module_->types().pointer_type(type->element);
                        create_call_instruction(constructor, {element}, nullptr,
                                             "array.constructor");
                    }
                }
                auto* destructor = ensure_member_destructor(record->second);
                if (destructor) {
                    local_cleanups_.push_back(
                        {address, destructor, type->count, type->element});
                    note_scope_cleanup();
                }
            }
        }
        if (frontend_type && frontend_type->kind == frontend::TypeKind::Record &&
            frontend_type->record && frontend_type->record->polymorphic) {
            emit_vtable_init(address, type);
        }
        if (frontend_type && frontend_type->is_reference) {
            if (init->children.size() > 1 && init->children[1]) {
                Expression target;
                if (init->children[1]->kind ==
                    frontend::AstKind::CallExpression) {
                    // A record value is its address, and a reference-returning
                    // call answers with one, so the binding names the object
                    // rather than a copy of it (ADR 0036).
                    auto value = emit_expression(*init->children[1]);
                    if (value.value && value.type &&
                        (value.type->kind == ir::TypeKind::Struct ||
                         (address->type && address->type->element &&
                          ir::same_type(value.type,
                                         address->type->element)))) {
                        target.value = value.value;
                        target.type = value.type;
                    }
                } else {
                    target = emit_lvalue(*init->children[1]);
                }
                if (target.value) {
                    // The slot is a pointer to the referenced type, so the value
                    // stored into it is compared with the slot's element. Two
                    // structurally identical IR types are the same type even when
                    // the pointers differ (ADR 0036).
                    if (address->type && address->type->element &&
                        !ir::same_type(target.value->type,
                                       address->type->element)) {
                        target.value = builder_.create(
                            ir::Opcode::BitCast, address->type->element,
                            {target.value}, "reference.cast");
                    }
                    builder_.create_store(target.value, address);
                }
            }
            continue;
        }
        if (init->children.size() > 1 && init->children[1] &&
            type && type->kind == ir::TypeKind::Struct &&
            init->children[1]->kind == frontend::AstKind::CallExpression &&
            !init->children[1]->children.empty() &&
            init->children[1]->children.front() &&
            init->children[1]->children.front()->kind ==
                frontend::AstKind::Identifier &&
            functions_.contains(
                init->children[1]->children.front()->text)) {
            auto value = emit_expression(*init->children[1]);
            if (value.value && value.type == type) {
                builder_.create_store(value.value, address);
                continue;
            }
        }
        if (init->children.size() > 1 && init->children[1] &&
            init->children[1]->kind == frontend::AstKind::ExpressionStatement &&
            type && type->kind != ir::TypeKind::Struct) {
            // `int x(7)` initializes a scalar directly from the single value in
            // the parenthesis.
            const auto& scalar_arguments = init->children[1]->children;
            if (scalar_arguments.size() == 1 && scalar_arguments.front()) {
                auto value = emit_expression(*scalar_arguments.front());
                if (value.value) {
                    value.value = promote(value.value, value.type, type);
                    builder_.create_store(value.value, address);
                }
            } else {
                error(declaration.location,
                      "parenthesized initializer takes exactly one value");
            }
            continue;
        }
        if (init->children.size() > 1 && init->children[1] &&
            init->children[1]->kind == frontend::AstKind::ExpressionStatement &&
            type && type->kind == ir::TypeKind::Struct) {
            const auto record = ir_records_.find(type);
            ir::Function* constructor = nullptr;
            const auto& argument_nodes = init->children[1]->children;
            int reference_mode = 0;
            if (!argument_nodes.empty() && argument_nodes.front()) {
                if (argument_nodes.front()->kind == frontend::AstKind::Identifier) {
                    if (auto* local = lookup_local(argument_nodes.front()->text)) {
                        // A reference mode of 1 asks for a **copy construction**, so it
                        // applies only when the argument is a record of this very type.
                        // Setting it for any record lvalue made `Consumer consumer(large)`
                        // ask for a reference parameter and refuse the by-value
                        // constructor it actually has.
                        const bool same_record =
                            type && type->kind == ir::TypeKind::Struct &&
                            record != ir_records_.end() && record->second &&
                            local->frontend_type &&
                            local->frontend_type->kind == frontend::TypeKind::Record &&
                            local->frontend_type->record &&
                            local->frontend_type->record->tag == record->second->tag;
                        if (same_record) {
                            reference_mode = 1;
                        }
                    }
                } else if (argument_nodes.front()->kind ==
                               frontend::AstKind::CastExpression &&
                           !argument_nodes.front()->children.empty() &&
                           argument_nodes.front()->children.front()) {
                    const auto cast_type = sema_.type_from_type_name(
                        *argument_nodes.front()->children.front());
                    if (cast_type && cast_type->is_rvalue_reference) {
                        reference_mode = 2;
                    }
                } else if (is_move_call(*argument_nodes.front())) {
                    reference_mode = 2;
                }
            }
            if (record != ir_records_.end()) {
                constructor = select_constructor(record->second->tag,
                                                 argument_nodes.size(),
                                                 reference_mode);
            }
            if (!constructor) {
            }
            if (!constructor) {
                if (argument_nodes.size() == 1 && argument_nodes.front()) {
                    // A record is copied field by field from its own address. A
                    // whole-record load and store moves only the first word in
                    // the backends, which is why a two-field copy kept its first
                    // field and zeroed the rest. A temporary has no address, so
                    // the value path below still handles it.
                    // Only an expression that names a place can be copied from an
                    // address; mit_lvalue reports an error on anything else, so
                    // the shape is checked first and a literal argument keeps the
                    // value path.
                    const auto argument_kind = argument_nodes.front()->kind;
                    const bool lvalue_shaped =
                        argument_kind == frontend::AstKind::Identifier ||
                        argument_kind == frontend::AstKind::MemberExpression ||
                        argument_kind == frontend::AstKind::CallExpression ||
                        (argument_kind == frontend::AstKind::UnaryExpression &&
                         argument_nodes.front()->text == "*");
                    if (lvalue_shaped &&
                        (type->kind == ir::TypeKind::Struct ||
                         type->kind == ir::TypeKind::Array)) {
                        auto source = emit_lvalue(*argument_nodes.front());
                        if (source.value) {
                            emit_copy(address, source.value, type);
                            // The object now exists, so its destructor has to run at
                            // scope exit exactly as it would had a constructor made it.
                            // Skipping that registration leaked every copy of a class
                            // that declares a destructor: two objects, one destructor.
                            if (type->kind == ir::TypeKind::Struct &&
                                record != ir_records_.end() && record->second) {
                                if (ir::Function* cleanup =
                                        ensure_member_destructor(record->second)) {
                                    local_cleanups_.push_back(
                                        {address, cleanup, 1, nullptr});
                                    note_scope_cleanup();
                                }
                            }
                            continue;
                        }
                    }
                    auto value = emit_expression(*argument_nodes.front());
                    if (value.value && value.type == type) {
                        builder_.create_store(value.value, address);
                        continue;
                    }
                }
                if (record != ir_records_.end() && !argument_nodes.empty()) {
                    for (std::size_t field_position = 0;
                         field_position < type->fields.size(); ++field_position) {
                        auto* zero = module_->constant_int(int_type(), 0);
                        auto* field_index = module_->constant_int(
                            int_type(), static_cast<std::int64_t>(field_position));
                        auto* field = builder_.create_gep(
                            type, address, {zero, field_index}, false,
                            "brace.field");
                        field->type =
                            module_->types().pointer_type(type->fields[field_position]);
                        if (field_position < argument_nodes.size() &&
                            argument_nodes[field_position]) {
                            emit_initializer(*argument_nodes[field_position], field,
                                             type->fields[field_position]);
                        } else {
                            emit_zero_initializer(field, type->fields[field_position]);
                        }
                    }
                    continue;
                }
                error(declaration.location, "constructor-initialized class has no constructor");
                continue;
            }
            std::vector<ir::Value*> arguments{address};
            for (std::size_t argument_index = 0; argument_index < argument_nodes.size();
                 ++argument_index) {
                if (!argument_nodes[argument_index]) {
                    continue;
                }
                auto argument = emit_expression(*argument_nodes[argument_index]);
                if (!argument.value) {
                    break;
                }
                const auto parameter_index = argument_index + 1;
                if (parameter_index < constructor->type->parameters.size()) {
                    bool reference_parameter = false;
                    const auto ast_type = function_ast_types_.find(constructor);
                    if (ast_type != function_ast_types_.end() && ast_type->second &&
                        argument_index < ast_type->second->parameters.size() &&
                        ast_type->second->parameters[argument_index] &&
                        ast_type->second->parameters[argument_index]->is_reference) {
                        reference_parameter = true;
                    }
                    if (reference_parameter) {
                        const frontend::AstNode* source = argument_nodes[argument_index].get();
                        if (source && source->kind == frontend::AstKind::CastExpression &&
                            source->children.size() > 1 && source->children[1]) {
                            source = source->children[1].get();
                        } else if (source && is_move_call(*source)) {
                            const auto& move_arguments = source->children[1]->children;
                            if (!move_arguments.empty() && move_arguments.front()) {
                                source = move_arguments.front().get();
                            }
                        }
                        if (source) {
                            auto address_argument = emit_lvalue(*source);
                            if (address_argument.value) {
                                argument = address_argument;
                                argument.type = argument.value->type;
                            }
                        }
                    }
                    argument.value = promote(argument.value, argument.type,
                                             constructor->type->parameters[parameter_index]);
                }
                arguments.push_back(argument.value);
            }
            create_call_instruction(constructor, std::move(arguments), nullptr, "ctor.call");
        } else if (init->children.size() > 1 && init->children[1]) {
            bool copied = false;
            bool move_source = false;
            if (type && type->kind == ir::TypeKind::Struct &&
                init->children[1]->kind == frontend::AstKind::CastExpression &&
                init->children[1]->children.size() > 1 &&
                init->children[1]->children.front()) {
                const auto cast_type = sema_.type_from_type_name(
                    *init->children[1]->children.front());
                move_source = cast_type && cast_type->is_rvalue_reference;
            } else if (is_move_call(*init->children[1])) {
                move_source = true;
            }
            if (type && type->kind == ir::TypeKind::Struct &&
                (init->children[1]->kind == frontend::AstKind::Identifier ||
                 move_source)) {
                const auto record = ir_records_.find(type);
                if (record != ir_records_.end()) {
                    auto* copy_constructor = select_constructor(
                        record->second->tag, 1, move_source ? 2 : 1);
                    auto source = emit_lvalue(
                        move_source
                            ? (is_move_call(*init->children[1])
                                   ? *init->children[1]->children[1]->children.front()
                                   : *init->children[1]->children[1])
                            : *init->children[1]);
                    if (copy_constructor && source.value) {
                        std::vector<ir::Value*> arguments{address,
                                                          source.value};
                        if (arguments[0]->type !=
                            copy_constructor->type->parameters[0]) {
                            arguments[0] = builder_.create(
                                ir::Opcode::BitCast,
                                copy_constructor->type->parameters[0],
                                {arguments[0]}, "ctor.this");
                        }
                        create_call_instruction(copy_constructor, std::move(arguments),
                                             nullptr, "copy.ctor");
                        copied = true;
                    }
                }
            }
            // No copy constructor was available and the initializer names an object of
            // the same record type. Copy the fields instead of falling through to the
            // constructor path below, which selected an arity-one **converting**
            // constructor and passed the record where its parameter type belonged --
            // the `call argument type mismatch` the IR verifier reported for
            // `Counter copy(original)` on a class that declares a destructor.
            if (!copied && type && type->kind == ir::TypeKind::Struct &&
                init->children.size() > 1 && init->children[1] &&
                init->children[1]->kind == frontend::AstKind::Identifier) {
                const auto source = emit_lvalue(*init->children[1]);
                if (source.value && source.value->type &&
                    source.value->type->kind == ir::TypeKind::Pointer &&
                    source.value->type->element == type) {
                    copy_record_fields(source.value, address, type);
                    copied = true;
                }
            }
            if (!copied) {
                emit_initializer(*init->children[1], address, type,
                                 frontend_type && frontend_type->is_volatile);
            }
        } else if (type && type->kind == ir::TypeKind::Struct) {
            const auto record = ir_records_.find(type);
            ir::Function* constructor = nullptr;
            if (record != ir_records_.end()) {
                constructor = select_constructor(record->second->tag, 0);
                if (!constructor) {
                    constructor = ensure_default_constructor(record->second);
                }
            }
            if (!constructor && record != ir_records_.end()) {
                constructor = ensure_default_constructor(record->second);
            }
            // A constructor that exists only as a class template pattern's declaration
            // has no body to call: pattern bodies are written for stub types and are
            // never built (ADR 0032). An instance with no members has nothing to
            // construct, so the call is skipped. Emitting it left `std::tuple<> empty;`
            // bound to the pattern's `tuple_ctor_0_value`, which nothing defines.
            if (constructor && constructor->declaration &&
                record->second->members.empty()) {
                constructor = nullptr;
            }
            if (constructor) {
                auto* constructor_this = address;
                if (constructor_this->type != constructor->type->parameters[0]) {
                    constructor_this = builder_.create(
                        ir::Opcode::BitCast, constructor->type->parameters[0],
                        {constructor_this}, "ctor.this");
                }
                create_call_instruction(constructor, {constructor_this}, nullptr, "ctor.call");
            } else if (record != ir_records_.end()) {
                std::vector<const frontend::RecordInfo*> levels;
                for (auto* current = record->second; current != nullptr;
                     current = current->base && current->base->record
                                   ? current->base->record.get()
                                   : nullptr) {
                    levels.push_back(current);
                }
                if (record->second->base &&
                    record->second->base->kind ==
                        frontend::TypeKind::Record &&
                    record->second->base->record) {
                    auto* base_constructor = select_constructor(
                        record->second->base->record->tag, 0);
                    if (base_constructor) {
                        auto* base_this = address;
                        if (base_this->type !=
                            base_constructor->type->parameters[0]) {
                            base_this = builder_.create(
                                ir::Opcode::BitCast,
                                base_constructor->type->parameters[0],
                                {base_this}, "default.base.this");
                        }
                        create_call_instruction(base_constructor, {base_this},
                                             nullptr, "default.base.ctor");
                    }
                }
                std::reverse(levels.begin(), levels.end());
                for (const auto* level : levels) {
                    const auto level_type = record_types_.find(level);
                    if (level_type == record_types_.end()) {
                        continue;
                    }
                    for (std::size_t member_index = 0;
                         member_index < level->members.size();
                         ++member_index) {
                        const auto& member = level->members[member_index];
                        const auto named = std::find(type->field_names.begin(),
                                                     type->field_names.end(),
                                                     member.name);
                        if (named == type->field_names.end()) {
                            continue;
                        }
                        const auto position = static_cast<std::size_t>(
                            std::distance(type->field_names.begin(), named));
                        if (position >= type->fields.size()) {
                            continue;
                        }
                        auto* zero = module_->constant_int(int_type(), 0);
                        auto* field_index = module_->constant_int(
                            int_type(),
                            static_cast<std::int64_t>(position));
                        auto* field = builder_.create_gep(
                            type, address, {zero, field_index}, false,
                            "default.field");
                        field->type = module_->types().pointer_type(
                            type->fields[position]);
                        if (member.default_initializer &&
                            !member.default_initializer->children.empty() &&
                            member.default_initializer->children.front()) {
                            emit_initializer(
                                *member.default_initializer->children.front(),
                                field, type->fields[position]);
                            continue;
                        }
                        if (member.type &&
                            member.type->kind == frontend::TypeKind::Array &&
                            member.type->element &&
                            member.type->element->record) {
                            const auto element_record = ir_records_.find(
                                type->fields[position]->element);
                            if (element_record != ir_records_.end() &&
                                element_record->second) {
                                auto* element_constructor = select_constructor(
                                    element_record->second->tag, 0);
                                if (element_constructor) {
                                    for (std::uint64_t element_position = 0;
                                         element_position <
                                         type->fields[position]->count;
                                         ++element_position) {
                                        auto* element_index =
                                            module_->constant_int(
                                                int_type(),
                                                static_cast<std::int64_t>(
                                                    element_position));
                                        auto* element =
                                            builder_.create_gep(
                                                type->fields[position], field,
                                                {zero, element_index}, false,
                                                "member.ctor.element");
                                        element->type =
                                            module_->types().pointer_type(
                                                type->fields[position]
                                                    ->element);
                                        auto* self = element;
                                        if (self->type !=
                                            element_constructor->type
                                                ->parameters[0]) {
                                            self = builder_.create(
                                                ir::Opcode::BitCast,
                                                element_constructor->type
                                                    ->parameters[0],
                                                {self}, "member.ctor.this");
                                        }
                                        create_call_instruction(
                                            element_constructor, {self},
                                            nullptr, "member.ctor");
                                    }
                                }
                            }
                            continue;
                        }
                        if (!member.type ||
                            member.type->kind !=
                                frontend::TypeKind::Record ||
                            !member.type->record) {
                            continue;
                        }
                        const auto member_record =
                            ir_records_.find(type->fields[position]);
                        if (member_record == ir_records_.end() ||
                            !member_record->second) {
                            continue;
                        }
                        auto* member_constructor = select_constructor(
                            member_record->second->tag, 0);
                        if (!member_constructor) {
                            continue;
                        }
                        auto* self = field;
                        if (self->type !=
                            member_constructor->type->parameters[0]) {
                            self = builder_.create(
                                ir::Opcode::BitCast,
                                member_constructor->type->parameters[0],
                                {self}, "member.this");
                        }
                        create_call_instruction(member_constructor, {self},
                                             nullptr, "member.ctor");
                    }
                }
            }
        }
        if (type && type->kind == ir::TypeKind::Struct) {
            const auto record = ir_records_.find(type);
            if (record != ir_records_.end()) {
                ir::Function* cleanup =
                    ensure_member_destructor(record->second);
                if (cleanup) {
                    local_cleanups_.push_back({address, cleanup, 1, nullptr});
                    note_scope_cleanup();
                }
            }
        }
    }
}

void CIRGen::emit_initializer(const frontend::AstNode& initializer, ir::Value* address,
                              ir::TypePtr type, bool is_volatile) {
    if (initializer.kind == frontend::AstKind::CompoundLiteral &&
        initializer.children.size() > 1 && initializer.children[1]) {
        emit_initializer(*initializer.children[1], address, type);
        return;
    }
    auto* conversion_local =
        initializer.kind == frontend::AstKind::Identifier
            ? lookup_local(initializer.text)
            : nullptr;
    const bool conversion_call =
        initializer.kind == frontend::AstKind::CallExpression;
    if (type && (ir::is_integer(type) || ir::is_float(type)) &&
        (conversion_call ||
         (conversion_local && conversion_local->frontend_type &&
          conversion_local->frontend_type->kind ==
              frontend::TypeKind::Record))) {
        Expression source;
        if (conversion_call) {
            auto value = emit_expression(initializer);
            if (value.value && value.type &&
                value.type->kind == ir::TypeKind::Struct) {
                auto* storage = builder_.create_alloca(
                    value.type, 0, "init.temp");
                builder_.create_store(value.value, storage);
                source.value = storage;
                source.type = value.type;
            } else if (value.value) {
                builder_.create_store(
                    promote(value.value, value.type, type), address);
                return;
            }
        } else {
            source = emit_lvalue(initializer);
        }
        if (source.value && source.type &&
            source.type->kind == ir::TypeKind::Struct) {
            const auto record = ir_records_.find(source.type);
            if (record != ir_records_.end() && record->second) {
                const auto methods = class_methods_.find(record->second->tag);
                if (methods != class_methods_.end()) {
                    const char* conversion = nullptr;
                    if (ir::is_integer(type)) {
                        conversion =
                            type->bits == 1 ? "operator bool" : "operator int";
                    } else {
                        conversion = type->bits == 64 ? "operator double"
                                                      : "operator float";
                    }
                    const auto method = methods->second.find(conversion);
                    if (method != methods->second.end()) {
                        auto self = source;
                        if (self.value->type !=
                            method->second->type->parameters[0]) {
                            self.value = builder_.create(
                                ir::Opcode::BitCast,
                                method->second->type->parameters[0],
                                {self.value}, "init.this");
                        }
                        auto* call = create_call_instruction(
                            method->second, {self.value},
                            method->second->type->return_type,
                            "init.conversion");
                        auto* converted =
                            promote(call, method->second->type->return_type, type);
                        builder_.create_store(converted, address);
                        return;
                    }
                }
            }
        }
    }

    if (type && type->kind == ir::TypeKind::Array && type->element &&
        ir::is_integer(type->element) && type->element->bits == 8 &&
        initializer.kind == frontend::AstKind::StringLiteral) {
        const auto bytes = decode_string(initializer.text);
        for (std::size_t index = 0; index < type->count; ++index) {
            auto* zero = module_->constant_int(int_type(), 0);
            auto* element_index = module_->constant_int(
                int_type(), static_cast<std::int64_t>(index));
            auto* element = builder_.create_gep(
                type, address, {zero, element_index}, false, "string.byte");
            element->type = module_->types().pointer_type(type->element);
            const auto value =
                index < bytes.size()
                    ? static_cast<unsigned char>(bytes[index])
                    : 0;
            builder_.create_store(
                module_->constant_int(type->element, value), element);
        }
        return;
    }
    if (type && type->kind == ir::TypeKind::Struct &&
        initializer.kind != frontend::AstKind::InitializerList) {
        const bool scalar_initializer =
            initializer.kind == frontend::AstKind::IntegerLiteral ||
            initializer.kind == frontend::AstKind::FloatLiteral ||
            initializer.kind == frontend::AstKind::CharLiteral ||
            initializer.kind == frontend::AstKind::StringLiteral;
        if (scalar_initializer) {
            const auto record = ir_records_.find(type);
            if (record != ir_records_.end() && record->second) {
                auto* constructor =
                    select_constructor(record->second->tag, 1, 0);
                if (constructor && constructor->type &&
                    constructor->type->parameters.size() == 2) {
                    auto argument = emit_expression(initializer);
                    if (argument.value) {
                        argument.value = promote(
                            argument.value, argument.type,
                            constructor->type->parameters[1]);
                        auto* self = address;
                        if (self->type != constructor->type->parameters[0]) {
                            self = builder_.create(
                                ir::Opcode::BitCast,
                                constructor->type->parameters[0], {self},
                                "init.this");
                        }
                        create_call_instruction(
                            constructor, {self, argument.value}, nullptr,
                            "init.constructor");
                        return;
                    }
                }
            }
        }
        if (initializer.kind == frontend::AstKind::PostfixExpression ||
            initializer.kind == frontend::AstKind::CallExpression) {
            auto value = emit_expression(initializer);
            if (value.value && value.type &&
                value.type->kind == ir::TypeKind::Struct) {
                builder_.create_store(value.value, address);
                return;
            }
        }
        auto source = emit_lvalue(initializer);
        if (source.value && source.type && source.type->kind == ir::TypeKind::Struct) {
            emit_copy(address, source.value, type);
            return;
        }
    }
    if (initializer.kind == frontend::AstKind::InitializerList) {
        const auto member_address = [&](ir::TypePtr aggregate, std::size_t index,
                                        ir::TypePtr member_type) {
            auto* zero = module_->constant_int(int_type(), 0);
            auto* member_index = module_->constant_int(int_type(),
                                                       static_cast<std::int64_t>(index));
            auto* member = builder_.create_gep(aggregate, address, {zero, member_index},
                                               false, "init.member");
            member->type = module_->types().pointer_type(member_type);
            return member;
        };
        if (type && type->kind == ir::TypeKind::Struct) {
            const auto* init_record =
                ir_records_.find(type) == ir_records_.end()
                    ? nullptr
                    : ir_records_.find(type)->second;
            const auto store_member =
                [&](ir::Value* base, ir::TypePtr struct_type,
                    std::size_t index, const frontend::AstNode& value) {
                const auto member_type = struct_type->fields[index];
                const frontend::RecordInfo* member_record = nullptr;
                if (const auto found = ir_records_.find(struct_type);
                    found != ir_records_.end()) {
                    member_record = found->second;
                }
                if (member_record && index < member_record->members.size() &&
                    member_record->members[index].bit_field &&
                    member_record->members[index].bit_width > 0) {
                    const auto offsets =
                        sema_.record_member_offsets(*member_record);
                    const auto byte_offset = offsets[index];
                    std::uint32_t bit_offset = 0;
                    for (std::size_t previous = 0; previous < index;
                         ++previous) {
                        if (member_record->members[previous].bit_field &&
                            offsets[previous] == byte_offset &&
                            member_record->members[previous].bit_width > 0) {
                            bit_offset +=
                                member_record->members[previous].bit_width;
                        }
                    }
                    auto* byte_type = module_->types().int_type(8, false);
                    auto* byte_pointer = builder_.create(
                        ir::Opcode::BitCast,
                        module_->types().pointer_type(byte_type), {base},
                        "init.bitfield.byte");
                    auto* offset = module_->constant_int(
                        int_type(),
                        static_cast<std::int64_t>(byte_offset));
                    auto* slot = builder_.create_gep(
                        byte_type, byte_pointer, {offset}, false,
                        "init.bitfield");
                    slot->type = module_->types().pointer_type(member_type);
                    auto value_expression = emit_expression(value);
                    if (!value_expression.value) {
                        return;
                    }
                    auto* converted = promote(value_expression.value,
                                              value_expression.type,
                                              member_type);
                    auto* old = builder_.create_load(
                        member_type, slot, false, 0, "init.bitfield.old");
                    const auto width =
                        member_record->members[index].bit_width;
                    const std::uint64_t field_mask =
                        width >= 64 ? ~0ull : ((1ull << width) - 1);
                    auto* mask = module_->constant_int(member_type,
                                                       field_mask);
                    auto* inverse = module_->constant_int(
                        member_type,
                        ~(field_mask << bit_offset));
                    auto* cleared =
                        builder_.create_and(old, inverse, "init.bitfield.clear");
                    auto* narrowed =
                        builder_.create_and(converted, mask, "init.bitfield.value");
                    auto* shift = module_->constant_int(member_type,
                                                        bit_offset);
                    auto* shifted =
                        builder_.create_shl(narrowed, shift, "init.bitfield.shift");
                    auto* combined =
                        builder_.create_or(cleared, shifted,
                                           "init.bitfield.combine");
                    builder_.create_store(combined, slot, false, 0);
                    return;
                }
                auto* member = member_address(struct_type, index, member_type);
                emit_initializer(value, member, member_type);
            };
            const bool designated = std::any_of(
                initializer.children.begin(), initializer.children.end(),
                [](const std::unique_ptr<frontend::AstNode>& child) {
                    return child && child->kind == frontend::AstKind::DesignatedInitializer;
                });
            if (designated) {
                emit_zero_initializer(address, type);
            }
            std::size_t positional = 0;
            for (const auto& element : initializer.children) {
                if (!element) {
                    continue;
                }
                if (element->kind == frontend::AstKind::DesignatedInitializer) {
                    if (element->children.size() < 2 || !element->children.front() ||
                        element->children.front()->kind != frontend::AstKind::Identifier) {
                        continue;
                    }
                    const auto& value = *element->children.back();
                    const std::size_t path_count = element->children.size() - 1;
                    if (path_count == 1 && init_record &&
                        element->children.front()) {
                        for (std::size_t index = 0;
                             index < init_record->members.size(); ++index) {
                            if (init_record->members[index].name ==
                                element->children.front()->text) {
                                store_member(address, type, index, value);
                                break;
                            }
                        }
                        continue;
                    }
                    std::function<bool(ir::Value*, ir::TypePtr, std::size_t)> apply =
                        [&](ir::Value* target, ir::TypePtr target_type,
                            std::size_t path) -> bool {
                        if (path >= path_count) {
                            emit_initializer(value, target, target_type);
                            return true;
                        }
                        const auto* designator = element->children[path].get();
                        if (!designator || !target_type ||
                            target_type->kind != ir::TypeKind::Struct) {
                            return false;
                        }
                        const auto record = ir_records_.find(target_type);
                        if (record == ir_records_.end() || !record->second) {
                            return false;
                        }
                        std::size_t field_index = target_type->fields.size();
                        for (std::size_t index = 0;
                             index < record->second->members.size(); ++index) {
                            if (record->second->members[index].name == designator->text) {
                                field_index = index;
                                break;
                            }
                        }
                        if (field_index >= target_type->fields.size()) {
                            return false;
                        }
                        if (path + 1 == path_count) {
                            store_member(target, target_type, field_index,
                                         value);
                            return true;
                        }
                        auto* zero = module_->constant_int(int_type(), 0);
                        auto* member_index = module_->constant_int(
                            int_type(), static_cast<std::int64_t>(field_index));
                        auto* member = builder_.create_gep(
                            target_type, target, {zero, member_index}, false,
                            "init.designated");
                        member->type = module_->types().pointer_type(
                            target_type->fields[field_index]);
                        return apply(member, target_type->fields[field_index], path + 1);
                    };
                    static_cast<void>(apply(address, type, 0));
                    continue;
                }
                if (positional < type->fields.size()) {
                    store_member(address, type, positional, *element);
                }
                ++positional;
            }
            return;
        }
        if (type && type->kind == ir::TypeKind::Array) {
            const bool designated = std::any_of(
                initializer.children.begin(), initializer.children.end(),
                [](const std::unique_ptr<frontend::AstNode>& child) {
                    return child && child->kind == frontend::AstKind::DesignatedInitializer;
                });
            if (designated) {
                emit_zero_initializer(address, type);
            }
            std::size_t positional = 0;
            for (const auto& element : initializer.children) {
                if (!element) {
                    continue;
                }
                std::size_t index = positional;
                const frontend::AstNode* value = element.get();
                if (element->kind == frontend::AstKind::DesignatedInitializer) {
                    if (element->children.empty()) {
                        continue;
                    }
                    long long constant = 0;
                    if (element->children.size() > 1 &&
                        sema_.evaluate_constant_expression(*element->children.front(),
                                                           constant) &&
                        constant >= 0) {
                        index = static_cast<std::size_t>(constant);
                    }
                    value = element->children.back().get();
                }
                if (index < type->count && value) {
                    auto* member = member_address(type, index, type->element);
                    emit_initializer(*value, member, type->element);
                }
                ++positional;
            }
            return;
        }
        error(initializer.location, "aggregate initializer requires an aggregate type");
        return;
    }
    const auto value = emit_expression(initializer);
    auto* converted = promote(value.value, value.type, type);
    if (converted) {
        builder_.create_store(converted, address, is_volatile, 0);
    }
}

void CIRGen::emit_zero_initializer(ir::Value* address, ir::TypePtr type) {
    if (!type) {
        return;
    }
    if (type->kind == ir::TypeKind::Struct || type->kind == ir::TypeKind::Array) {
        const auto count = type->kind == ir::TypeKind::Struct ? type->fields.size() : type->count;
        for (std::size_t index = 0; index < count; ++index) {
            const auto member_type =
                type->kind == ir::TypeKind::Struct ? type->fields[index] : type->element;
            auto* zero = module_->constant_int(int_type(), 0);
            auto* member_index = module_->constant_int(int_type(),
                                                       static_cast<std::int64_t>(index));
            auto* member = builder_.create_gep(type, address, {zero, member_index}, false,
                                               "zero.member");
            member->type = module_->types().pointer_type(member_type);
            emit_zero_initializer(member, member_type);
        }
        return;
    }
    ir::Value* zero = nullptr;
    if (ir::is_integer(type)) {
        zero = module_->constant_int(type, 0);
    } else if (ir::is_pointer(type)) {
        zero = module_->constant_null(type);
    } else if (ir::is_float(type)) {
        zero = module_->constant_float(type, 0.0);
    }
    if (zero) {
        builder_.create_store(zero, address, false, 0);
    }
}

void CIRGen::emit_branch(ir::BasicBlock* target) {
    if (builder_.insert_block() && !builder_.insert_block()->terminated()) {
        builder_.create_br(target);
    }
}

void CIRGen::copy_record_fields(ir::Value* source, ir::Value* target,
                                ir::TypePtr type) {
    if (type && type->kind == ir::TypeKind::Struct) {
        auto* zero = module_->constant_int(int_type(), 0);
        for (std::size_t field = 0; field < type->fields.size(); ++field) {
            auto* index =
                module_->constant_int(int_type(), static_cast<std::int64_t>(field));
            auto* from = builder_.create_gep(type, source, {zero, index}, false,
                                             "deref.copy.source");
            from->type = module_->types().pointer_type(type->fields[field]);
            auto* to = builder_.create_gep(type, target, {zero, index}, false,
                                           "deref.copy.destination");
            to->type = module_->types().pointer_type(type->fields[field]);
            copy_record_fields(from, to, type->fields[field]);
        }
        return;
    }
    auto* loaded = builder_.create_load(type, source, false, 0, "deref.copy.value");
    builder_.create_store(loaded, target);
}

void CIRGen::emit_conditional_branch(ir::Value* condition, ir::BasicBlock* then_block,
                                     ir::BasicBlock* else_block) {
    if (!builder_.insert_block() || builder_.insert_block()->terminated()) {
        return;
    }
    // A null condition means the expression it came from already reported an error.
    // Branching on it would put a null operand in the IR, which the verifier rejects
    // and the back ends fault on, so the then-branch is skipped instead: the
    // diagnostic is already emitted, and this keeps the rest of the function valid.
    if (condition == nullptr) {
        builder_.create_br(else_block);
        return;
    }
    builder_.create_cond_br(condition, then_block, else_block);
}

void CIRGen::emit_return(const frontend::AstNode& statement) {
    if (statement.children.empty() || !statement.children.front()) {
        builder_.create_ret(nullptr);
        return;
    }
    if (statement.children.size() == 1 && statement.children.front() &&
        statement.children.front()->kind == frontend::AstKind::UnaryExpression &&
        statement.children.front()->text == "*" &&
        !statement.children.front()->children.empty() &&
        statement.children.front()->children.front() &&
        statement.children.front()->children.front()->kind ==
            frontend::AstKind::Identifier &&
        statement.children.front()->children.front()->text == "this") {
        auto pointer = emit_expression(*statement.children.front()->children.front());
        if (pointer.value) {
            emit_cleanups();
            builder_.create_ret(pointer.value);
            return;
        }
    }
    const auto ast_type = function_ast_types_.find(current_function_);
    if (ast_type != function_ast_types_.end() && ast_type->second &&
        ast_type->second->return_type &&
        ast_type->second->return_type->is_reference) {
        auto address = emit_lvalue(*statement.children.front());
        if (address.value && current_function_->type &&
            current_function_->type->return_type) {
            auto* value = address.value;
            // Structural, not pointer identity: an instance's return type is a
            // fresh Type object, so identity reported a difference where there was
            // none and emitted a cast from a type to itself, which the backend
            // cannot encode (ADR 0036).
            if (!value->type ||
                !ir::same_type(value->type, current_function_->type->return_type)) {
                value = builder_.create(ir::Opcode::BitCast,
                                        current_function_->type->return_type,
                                        {value}, "reference.return");
            }
            emit_cleanups_for_exit(0);
            builder_.create_ret(value);
            return;
        }
    }
    if (current_function_->type && current_function_->type->return_type &&
        (ir::is_integer(current_function_->type->return_type) ||
         ir::is_float(current_function_->type->return_type)) &&
        statement.children.front()->kind ==
            frontend::AstKind::CallExpression) {
        auto value = emit_expression(*statement.children.front());
        if (value.value && (!value.type ||
                            value.type->kind != ir::TypeKind::Struct)) {
            // The call already produced the value. Emitting it again here ran
            // its side effects twice: `return bump();` incremented twice, which
            // is how a forwarding wrapper read the wrong byte.
            auto converted = promote(value.value, value.type,
                                     current_function_->type->return_type);
            emit_cleanups_for_exit(0);
            builder_.create_ret(converted);
            return;
        }
        if (value.value && value.type &&
            value.type->kind == ir::TypeKind::Struct) {
            auto* storage = builder_.create_alloca(value.type, 0,
                                                       "return.temp");
            builder_.create_store(value.value, storage);
            const auto record = ir_records_.find(value.type);
            if (record != ir_records_.end() && record->second) {
                const auto methods =
                    class_methods_.find(record->second->tag);
                if (methods != class_methods_.end()) {
                    const auto& return_type =
                        current_function_->type->return_type;
                    const char* conversion = nullptr;
                    if (ir::is_integer(return_type)) {
                        conversion = return_type->bits == 1
                                         ? "operator bool"
                                         : "operator int";
                    } else {
                        conversion = return_type->bits == 64
                                         ? "operator double"
                                         : "operator float";
                    }
                    const auto method = methods->second.find(conversion);
                    if (method != methods->second.end()) {
                        auto* self = storage;
                        if (self->type !=
                            method->second->type->parameters[0]) {
                            self = builder_.create(
                                ir::Opcode::BitCast,
                                method->second->type->parameters[0],
                                {self}, "return.this");
                        }
                        auto* call = create_call_instruction(
                            method->second, {self},
                            method->second->type->return_type,
                            "return.conversion");
                        auto* converted = promote(
                            call, method->second->type->return_type,
                            return_type);
                        emit_cleanups_for_exit(0);
                        builder_.create_ret(converted);
                        return;
                    }
                }
            }
            // A record with no conversion is not a return value. Reporting it
            // here keeps the call from being emitted a second time by the
            // generic path below, which returned a record from an integer
            // function and left invalid IR behind.
            error(statement.location,
                  "the returned record has no conversion to the return type");
            emit_cleanups();
            builder_.create_ret(nullptr);
            return;
        }
    }
    if (current_function_->type && current_function_->type->return_type &&
        (ir::is_integer(current_function_->type->return_type) ||
         ir::is_float(current_function_->type->return_type)) &&
        statement.children.front()->kind == frontend::AstKind::Identifier) {
        if (auto* local = lookup_local(statement.children.front()->text)) {
            // A reference to a record names the record, so s[i] on a
            // reference parameter reaches operator[] like a direct object.
            const auto object_type = record_object_type(local->frontend_type);
            if (object_type && object_type->kind == frontend::TypeKind::Record &&
                object_type->record) {
                const auto methods = class_methods_.find(object_type->record->tag);
                if (methods != class_methods_.end()) {
                    const char* conversion = nullptr;
                    if (ir::is_integer(
                            current_function_->type->return_type)) {
                        conversion =
                            current_function_->type->return_type->bits == 1
                                ? "operator bool"
                                : "operator int";
                    } else {
                        conversion =
                            current_function_->type->return_type->bits == 64
                                ? "operator double"
                                : "operator float";
                    }
                    const auto method = methods->second.find(conversion);
                    if (method != methods->second.end()) {
                        auto self = emit_lvalue(*statement.children.front());
                        if (self.value) {
                            if (self.value->type !=
                                method->second->type->parameters[0]) {
                                self.value = builder_.create(
                                    ir::Opcode::BitCast,
                                    method->second->type->parameters[0],
                                    {self.value}, "return.this");
                            }
                            auto* call = create_call_instruction(
                                method->second, {self.value},
                                method->second->type->return_type,
                                "return.conversion");
                            auto* converted = promote(
                                call, method->second->type->return_type,
                                current_function_->type->return_type);
                            emit_cleanups_for_exit(0);
                            builder_.create_ret(converted);
                            return;
                        }
                    }
                }
            }
        }
    }
    if (current_function_->type && current_function_->type->return_type &&
        current_function_->type->return_type->kind == ir::TypeKind::Struct) {
        const auto kind = statement.children.front()->kind;
        if (kind == frontend::AstKind::InitializerList) {
            auto* storage = builder_.create_alloca(
                current_function_->type->return_type, 0, "return.temp");
            emit_initializer(*statement.children.front(), storage,
                             current_function_->type->return_type);
            auto* loaded = builder_.create_load(
                current_function_->type->return_type, storage, false, 0,
                "return.record");
            emit_cleanups_for_exit(0);
            builder_.create_ret(loaded);
            return;
        }
        const bool scalar = kind == frontend::AstKind::IntegerLiteral ||
                            kind == frontend::AstKind::FloatLiteral ||
                            kind == frontend::AstKind::CharLiteral ||
                            kind == frontend::AstKind::StringLiteral;
        if (scalar) {
            const auto record =
                ir_records_.find(current_function_->type->return_type);
            if (record != ir_records_.end() && record->second) {
                auto* constructor =
                    select_constructor(record->second->tag, 1, 0);
                if (constructor && constructor->type &&
                    constructor->type->parameters.size() == 2) {
                    auto value = emit_expression(*statement.children.front());
                    if (value.value) {
                        value.value = promote(
                            value.value, value.type,
                            constructor->type->parameters[1]);
                        auto* storage = builder_.create_alloca(
                            current_function_->type->return_type, 0,
                            "return.temp");
                        auto* self = storage;
                        if (self->type !=
                            constructor->type->parameters[0]) {
                            self = builder_.create(
                                ir::Opcode::BitCast,
                                constructor->type->parameters[0], {self},
                                "return.this");
                        }
                        create_call_instruction(
                            constructor, {self, value.value}, nullptr,
                            "return.constructor");
                        auto* loaded = builder_.create_load(
                            current_function_->type->return_type, storage,
                            false, 0, "return.record");
                        emit_cleanups_for_exit(0);
                        builder_.create_ret(loaded);
                        return;
                    }
                }
            }
        }
    }
    auto value = emit_expression(*statement.children.front());
    auto* converted = promote(value.value, value.type,
                              current_function_->type ? current_function_->type->return_type
                                                      : nullptr);
    emit_cleanups_for_exit(0);
    builder_.create_ret(converted);
}

ir::Value* CIRGen::emit_condition(const frontend::AstNode& expression) {
    auto value = emit_expression(expression);
    if (!value.value || !value.type) {
        return nullptr;
    }
    if (ir::is_integer(value.type) && value.type->bits == 1) {
        return value.value;
    }
    if (value.type->kind == ir::TypeKind::Struct) {
        const auto record = ir_records_.find(value.type);
        if (record != ir_records_.end() && record->second) {
            const auto methods = class_methods_.find(record->second->tag);
            if (methods != class_methods_.end()) {
                auto method = methods->second.find("operator bool");
                bool from_integer = false;
                if (method == methods->second.end()) {
                    method = methods->second.find("operator int");
                    from_integer = method != methods->second.end();
                }
                if (method != methods->second.end() && method->second) {
                    Expression self;
                    if (expression.kind ==
                        frontend::AstKind::CallExpression) {
                        auto record_value = emit_expression(expression);
                        if (record_value.value && record_value.type &&
                            record_value.type->kind ==
                                ir::TypeKind::Struct) {
                            auto* storage = builder_.create_alloca(
                                record_value.type, 0, "tobool.temp");
                            builder_.create_store(record_value.value, storage);
                            self.value = storage;
                            self.type = record_value.type;
                        }
                    } else {
                        self = emit_lvalue(expression);
                    }
                    if (self.value) {
                        if (self.value->type !=
                            method->second->type->parameters[0]) {
                            self.value = builder_.create(
                                ir::Opcode::BitCast,
                                method->second->type->parameters[0],
                                {self.value}, "tobool.this");
                        }
                        auto* call = create_call_instruction(
                            method->second, {self.value},
                            method->second->type->return_type, "tobool.call");
                        if (from_integer) {
                            auto* zero = module_->constant_int(
                                method->second->type->return_type, 0);
                            return builder_.create_icmp(
                                ir::ICmpPredicate::Ne, call, zero, "tobool.int");
                        }
                        return call;
                    }
                }
            }
        }
    }
    if (ir::is_pointer(value.type)) {
        return builder_.create_icmp(ir::ICmpPredicate::Ne, value.value,
                                    module_->constant_null(value.type), "tobool");
    }
    if (ir::is_float(value.type)) {
        return builder_.create_fcmp(ir::FCmpPredicate::One, value.value,
                                    module_->constant_float(value.type, 0.0), "tobool");
    }
    auto* zero = module_->constant_int(value.type, 0);
    return builder_.create_icmp(ir::ICmpPredicate::Ne, value.value, zero, "tobool");
}

CIRGen::Expression CIRGen::emit_expression(const frontend::AstNode& expression) {
    switch (expression.kind) {
    case frontend::AstKind::IntegerLiteral:
        return emit_integer_literal(expression);
    case frontend::AstKind::FloatLiteral: {
        const bool is_float = !expression.text.empty() &&
                              (expression.text.back() == 'f' || expression.text.back() == 'F');
        const auto type = module_->types().float_type(is_float ? 32 : 64);
        return {module_->constant_float(type, std::stod(expression.text)), type};
    }
    case frontend::AstKind::CharLiteral: {
        long long value = 0;
        if (!sema_.evaluate_constant_expression(expression, value)) {
            error(expression.location, "invalid character constant");
            return {};
        }
        const auto type = int_type();
        return {module_->constant_int(type, value), type};
    }
    case frontend::AstKind::StringLiteral:
        return emit_string_literal(expression);
    case frontend::AstKind::Identifier: {
        if (expression.text == "__func__" && current_function_) {
            frontend::AstNode node;
            node.kind = frontend::AstKind::StringLiteral;
            node.location = expression.location;
            const auto source_name =
                function_source_names_.find(current_function_);
            node.text = "\"" +
                        (source_name != function_source_names_.end()
                             ? source_name->second
                             : current_function_->name) +
                        "\"";
            return emit_string_literal(node);
        }
        if (auto* local = lookup_local(expression.text)) {
            if (local->frontend_type && local->frontend_type->is_reference &&
                local->type && local->type->kind == ir::TypeKind::Pointer) {
                auto* pointer = builder_.create_load(local->type, local->address, false, 0,
                                                     expression.text + ".ref");
                return {builder_.create_load(local->type->element, pointer, false, 0,
                                             expression.text + ".load"),
                        local->type->element};
            }
            if (local->type && local->type->kind == ir::TypeKind::Array) {
                auto* zero = module_->constant_int(int_type(), 0);
                auto* decay = builder_.create_gep(local->type, local->address, {zero, zero},
                                                  false, expression.text + ".decay");
                decay->type = module_->types().pointer_type(local->type->element);
                return {decay, decay->type};
            }
            const bool is_volatile =
                local->frontend_type && local->frontend_type->is_volatile;
            return {builder_.create_load(local->type, local->address,
                                         is_volatile, 0,
                                         expression.text + ".load"),
                    local->type};
        }
        auto* global = lookup_global(expression.text);
        if (in_static_method_ && current_class_) {
            global = lookup_global(current_class_->tag + "::" + expression.text);
        }
        if (global) {
            auto* variable = global;
            if (variable->element_type &&
                variable->element_type->kind == ir::TypeKind::Array) {
                auto* zero = module_->constant_int(int_type(), 0);
                auto* decay = builder_.create_gep(variable->element_type, variable, {zero, zero},
                                                  false, expression.text + ".decay");
                decay->type = module_->types().pointer_type(variable->element_type->element);
                return {decay, decay->type};
            }
            return {builder_.create_load(
                        variable->element_type, variable,
                        volatile_globals_.contains(variable), 0,
                        expression.text + ".load"),
                    variable->element_type};
        }
        if (current_class_) {
            bool has_member = false;
            for (auto* current = current_class_; current != nullptr;
                 current = current->base && current->base->record
                               ? current->base->record.get()
                               : nullptr) {
                if (std::any_of(current->members.begin(), current->members.end(),
                                [&](const frontend::RecordMember& member) {
                                    return member.name == expression.text;
                                })) {
                    has_member = true;
                    break;
                }
            }
            if (has_member) {
                return emit_implicit_member(expression.text);
            }
        }
        if (!lookup_function(expression.text) &&
            profile_.id == target::TargetId::Kr16 &&
            expression.text == "puts") {
            const auto ir_type = module_->types().function_type(
                int_type(),
                {module_->types().pointer_type(module_->types().int_type(8))},
                false);
            auto* declaration = module_->add_function(
                "puts", ir_type, ir::Linkage::External, true);
            declaration->declaration = true;
            declaration->add_parameter(
                module_->types().pointer_type(module_->types().int_type(8)),
                "text");
            functions_["puts"] = declaration;
        }
        if (!lookup_function(expression.text)) {
            const auto symbol = sema_.global_symbol(expression.text);
            if (symbol && symbol->kind == frontend::Sema::GlobalSymbol::Kind::Function &&
                symbol->type &&
                symbol->type->kind == frontend::TypeKind::Function) {
                const auto ir_type = map_type(symbol->type);
                if (ir_type && ir_type->kind == ir::TypeKind::Function) {
                    auto* declaration = module_->add_function(
                        expression.text, ir_type, ir::Linkage::External, true);
                    declaration->declaration = true;
                    for (std::size_t index = 0;
                         index < ir_type->parameters.size(); ++index) {
                        declaration->add_parameter(
                            ir_type->parameters[index],
                            "parameter" + std::to_string(index));
                    }
                    functions_[expression.text] = declaration;
                    function_ast_types_[declaration] = symbol->type;
                }
            }
        }
        auto* function = lookup_function(expression.text);
        if (function) {
            const auto type = module_->types().pointer_type(function->type);
            auto* address = builder_.create(ir::Opcode::BitCast, type, {function},
                                            "function.ptr");
            return {address, type};
        }
        long long constant = 0;
        if (sema_.evaluate_constant_expression(expression, constant)) {
            const auto type = int_type();
            return {module_->constant_int(type, constant), type};
        }
        error(expression.location, "identifier '" + expression.text + "' has no IR binding");
        return {};
    }
    case frontend::AstKind::UnaryExpression:
        return emit_unary(expression);
    case frontend::AstKind::PostfixExpression: {
        if (!expression.children.empty() && expression.children.front() &&
            expression.children.front()->kind == frontend::AstKind::Identifier) {
            if (auto* local = lookup_local(expression.children.front()->text)) {
                if (local->frontend_type &&
                    local->frontend_type->kind == frontend::TypeKind::Record &&
                    local->frontend_type->record) {
                    const auto methods =
                        class_methods_.find(local->frontend_type->record->tag);
                    if (methods != class_methods_.end()) {
                        auto method =
                            methods->second.find("operator" + expression.text + "#1");
                        if (method == methods->second.end()) {
                            method = methods->second.find(
                                "operator" + expression.text);
                        }
                        if (method == methods->second.end()) {
                            method = methods->second.find(
                                "operator" + expression.text + "#0");
                        }
                        if (method != methods->second.end()) {
                            auto self = emit_lvalue(*expression.children.front());
                            if (self.value) {
                                auto* self_value = self.value;
                                if (self_value->type !=
                                    method->second->type->parameters[0]) {
                                    self_value = builder_.create(
                                        ir::Opcode::BitCast,
                                        method->second->type->parameters[0],
                                        {self_value}, "post.this");
                                }
                                std::vector<ir::Value*> arguments{self_value};
                                if (method->second->type->parameters.size() > 1) {
                                    arguments.push_back(module_->constant_int(
                                        method->second->type->parameters[1], 0));
                                }
                                auto* call = create_call_instruction(
                                    method->second, std::move(arguments),
                                    method->second->type->return_type,
                                    "post.operator");
                                return reference_result(call, method->second, method->second->type->return_type);


                            }
                        }
                    }
                }
            }
        }
        auto address = emit_lvalue(*expression.children.front());
        if (!address.value) {
            return {};
        }
        const auto old = builder_.create_load(address.type, address.value, false, 0,
                                              "post.old");
        ir::Value* updated = nullptr;
        if (address.type && address.type->kind == ir::TypeKind::Pointer) {
            auto* one = module_->constant_int(int_type(), expression.text == "++" ? 1 : -1);
            updated = builder_.create_gep(address.type->element, old, {one}, false,
                                          "post.new");
            updated->type = address.type;
        } else {
            auto* one = module_->constant_int(address.type, 1);
            updated = expression.text == "++"
                          ? builder_.create_add(old, one, "post.new")
                          : builder_.create_sub(old, one, "post.new");
        }
        builder_.create_store(updated, address.value);
        return {old, address.type};
    }
    case frontend::AstKind::LambdaExpression:
        return emit_lambda(expression);
    case frontend::AstKind::ConditionalExpression:
        return emit_condition_expression(expression);
    case frontend::AstKind::BinaryExpression:
        return emit_binary(expression);
    case frontend::AstKind::AssignmentExpression:
        return emit_assignment(expression);
    case frontend::AstKind::CallExpression:
        return emit_call(expression);
    case frontend::AstKind::SubscriptExpression:
        return emit_subscript(expression);
    case frontend::AstKind::MemberExpression:
        return emit_member(expression);
    case frontend::AstKind::CompoundLiteral: {
        if (expression.children.size() < 2 || !expression.children[0] ||
            !expression.children[1]) {
            error(expression.location, "malformed compound literal");
            return {};
        }
        const auto frontend_type =
            sema_.type_from_type_name(*expression.children[0]);
        const auto type = map_type(frontend_type);
        if (!type) {
            return {};
        }
        auto* storage = builder_.create_alloca(type, 0, "compound.literal");
        emit_initializer(*expression.children[1], storage, type);
        return {storage, type};
    }
    case frontend::AstKind::CastExpression:
        return emit_cast(expression);
    case frontend::AstKind::TypeIdExpression:
        return emit_typeid(expression);
    case frontend::AstKind::GenericSelection: {
        if (expression.children.empty() || !expression.children.front()) {
            return {};
        }
        frontend::TypePtr control_type;
        const auto& control = *expression.children.front();
        if (control.kind == frontend::AstKind::Identifier) {
            if (const auto* local = lookup_local(control.text)) {
                control_type = local->frontend_type;
            }
        } else if (control.kind == frontend::AstKind::IntegerLiteral) {
            control_type = sema_.type_named("int");
        } else if (control.kind == frontend::AstKind::FloatLiteral) {
            control_type = sema_.type_named("double");
        } else if (control.kind == frontend::AstKind::CastExpression &&
                   !control.children.empty() && control.children.front()) {
            control_type = sema_.type_from_type_name(
                *control.children.front());
        }
        const frontend::AstNode* selected = nullptr;
        const frontend::AstNode* fallback = nullptr;
        std::function<bool(const frontend::TypePtr&,
                           const frontend::TypePtr&)>
            types_match;
        types_match = [&](const frontend::TypePtr& left,
                          const frontend::TypePtr& right) -> bool {
            if (!left || !right) {
                return false;
            }
            if (left->kind != right->kind) {
                return false;
            }
            if (left->kind == frontend::TypeKind::Pointer) {
                return types_match(left->element, right->element);
            }
            if (left->kind == frontend::TypeKind::Record) {
                return left->record && right->record &&
                       left->record->tag == right->record->tag;
            }
            return true;
        };
        for (std::size_t index = 1; index + 1 < expression.children.size();) {
            const auto* label = expression.children[index].get();
            if (!label) {
                break;
            }
            if (label->kind == frontend::AstKind::TypeName) {
                const auto label_type = sema_.type_from_type_name(*label);
                if (!selected && types_match(control_type, label_type)) {
                    selected = expression.children[index + 1].get();
                }
                index += 2;
            } else {
                fallback = expression.children[index].get();
                index += 1;
            }
        }
        const auto* chosen = selected ? selected : fallback;
        return chosen ? emit_expression(*chosen) : CIRGen::Expression{};
    }
    case frontend::AstKind::SizeofExpression:
    case frontend::AstKind::AlignofExpression:
        return emit_sizeof(expression);
    case frontend::AstKind::OffsetofExpression: {
        long long value = 0;
        if (!sema_.evaluate_constant_expression(expression, value)) {
            error(expression.location, "invalid offsetof expression");
            return {};
        }
        const auto type = size_type();
        return {module_->constant_int(type, value), type};
    }
    case frontend::AstKind::CommaExpression:
        if (expression.children.size() >= 2) {
            static_cast<void>(emit_expression(*expression.children[0]));
            return emit_expression(*expression.children[1]);
        }
        return {};
    default:
        error(expression.location, "expression kind is not implemented in IRGen yet");
        return {};
    }
}

CIRGen::Expression CIRGen::emit_lambda(const frontend::AstNode& expression) {
    if (expression.children.size() < 2 || !expression.children[0] ||
        !expression.children[1]) {
        return {};
    }
    const auto lambda_type = sema_.expression_type(expression);
    if (!lambda_type || lambda_type->kind != frontend::TypeKind::Pointer ||
        !lambda_type->element ||
        lambda_type->element->kind != frontend::TypeKind::Function) {
        error(expression.location, "cannot determine lambda type");
        return {};
    }
    const bool has_captures =
        !expression.labels.empty() || expression.children.size() > 2;
    if (has_captures) {
        std::vector<std::string> captures = expression.labels;
        const bool default_by_reference =
            std::find(captures.begin(), captures.end(), "&") != captures.end();
        const bool default_by_value =
            std::find(captures.begin(), captures.end(), "=") != captures.end();
        if (default_by_reference || default_by_value) {
            std::vector<std::string> names;
            for (const auto& scope : scopes_) {
                for (const auto& [name, binding] : scope) {
                    if (name != "this" && binding.frontend_type) {
                        names.push_back(name);
                    }
                }
            }
            std::sort(names.begin(), names.end());
            names.erase(std::unique(names.begin(), names.end()), names.end());
            captures.clear();
            for (const auto& name : names) {
                captures.push_back((default_by_reference ? "&" : "") + name);
            }
        }
        const auto byte_type = module_->types().int_type(8);
        std::vector<ir::TypePtr> fields{
            module_->types().pointer_type(byte_type)};
        std::vector<std::string> field_names{"__fn"};
        std::vector<frontend::TypePtr> capture_types;
        for (const auto& capture : captures) {
            const bool by_reference = capture.starts_with("&");
            const auto name =
                by_reference ? capture.substr(1) : std::string(capture);
            auto* local = lookup_local(name);
            if (!local || !local->frontend_type) {
                error(expression.location,
                      "unknown lambda capture '" + name + "'");
                return {};
            }
            const auto local_type = local->type ? local->type
                                                : map_type(local->frontend_type);
            const auto field_type =
                by_reference ? module_->types().pointer_type(local_type)
                             : local_type;
            fields.push_back(field_type);
            field_names.push_back(name);
            if (by_reference) {
                auto reference =
                    std::make_shared<frontend::Type>(*local->frontend_type);
                reference->is_reference = true;
                capture_types.push_back(std::move(reference));
            } else {
                capture_types.push_back(local->frontend_type);
            }
        }
        std::vector<std::pair<std::string, const frontend::AstNode*>>
            init_captures;
        for (std::size_t index = 2; index < expression.children.size();
             ++index) {
            const auto* capture = expression.children[index].get();
            if (!capture || capture->kind != frontend::AstKind::LambdaCapture ||
                capture->children.empty() || !capture->children.front()) {
                continue;
            }
            const auto capture_type =
                sema_.expression_type(*capture->children.front());
            if (!capture_type) {
                continue;
            }
            fields.push_back(map_type(capture_type));
            field_names.push_back(capture->text);
            capture_types.push_back(capture_type);
            init_captures.emplace_back(capture->text,
                                       capture->children.front().get());
        }
        const auto closure_type = module_->types().struct_type(
            "__lambda_closure_" + std::to_string(lambda_counter_), fields,
            field_names);
        std::vector<ir::TypePtr> parameters{
            module_->types().pointer_type(closure_type)};
        for (const auto& parameter : lambda_type->element->parameters) {
            parameters.push_back(parameter_ir_type(map_type(parameter)));
        }
        const auto function_type = module_->types().function_type(
            map_type(lambda_type->element->return_type), parameters, false);
        auto* function = module_->add_function(
            "__lambda_" + std::to_string(lambda_counter_++), function_type,
            ir::Linkage::Internal, false);
        function->add_parameter(module_->types().pointer_type(closure_type),
                                "__closure");

        const auto previous_function = current_function_;
        const auto previous_class = current_class_;
        auto* previous_block = builder_.insert_block();
        const auto previous_scopes = std::move(scopes_);
        scopes_.clear();
        current_function_ = function;
        current_class_ = nullptr;
        auto* entry = function->add_block("entry");
        builder_.reset_value_names();
        builder_.set_insert_point(entry);
        push_scope();
        local_cleanups_.clear();
        auto* closure_address = builder_.create_alloca(
            module_->types().pointer_type(closure_type), 0, "__closure.addr");
        builder_.create_store(function->parameters[0].get(), closure_address);
        declare_local("__closure",
                      LocalBinding{closure_address, function->parameters[0]->type,
                                   nullptr});
        const auto* declarator = expression.children[0].get();
        std::size_t parameter_index = 0;
        if (declarator) {
            for (std::size_t index = 1; index < declarator->children.size();
                 ++index) {
                const auto* parameter = declarator->children[index].get();
                if (!parameter ||
                    parameter->kind != frontend::AstKind::ParameterDeclaration ||
                    parameter_index >=
                        lambda_type->element->parameters.size()) {
                    continue;
                }
                function->add_parameter(
                    parameter_ir_type(map_type(lambda_type->element->parameters[parameter_index])),
                    parameter_name(*parameter, parameter_index));
                auto* parameter_address = builder_.create_alloca(
                    function->parameters[parameter_index + 1]->type, 0,
                    parameter_name(*parameter, parameter_index) + ".addr");
                builder_.create_store(
                    function->parameters[parameter_index + 1].get(),
                    parameter_address);
                declare_local(parameter_name(*parameter, parameter_index),
                              LocalBinding{parameter_address,
                                           function->parameters[parameter_index + 1]
                                               ->type,
                                           lambda_type->element
                                               ->parameters[parameter_index]});
                ++parameter_index;
            }
        }
        auto* closure_pointer = builder_.create_load(
            module_->types().pointer_type(closure_type), closure_address, false,
            0, "__closure.load");
        for (std::size_t index = 0; index < captures.size(); ++index) {
            auto* zero = module_->constant_int(int_type(), 0);
            auto* field_index = module_->constant_int(
                int_type(), static_cast<std::int64_t>(index + 1));
            auto* field = builder_.create_gep(
                closure_type, closure_pointer, {zero, field_index}, false,
                "capture." + captures[index]);
            field->type = module_->types().pointer_type(fields[index + 1]);
            const auto name = captures[index].starts_with("&")
                                  ? captures[index].substr(1)
                                  : captures[index];
            declare_local(name,
                          LocalBinding{field, fields[index + 1],
                                       capture_types[index]});
        }
        for (std::size_t index = 0; index < init_captures.size(); ++index) {
            auto* zero = module_->constant_int(int_type(), 0);
            const auto field_position = captures.size() + index + 1;
            auto* field_index = module_->constant_int(
                int_type(), static_cast<std::int64_t>(field_position));
            auto* field = builder_.create_gep(
                closure_type, closure_pointer, {zero, field_index}, false,
                "capture." + init_captures[index].first);
            field->type =
                module_->types().pointer_type(fields[field_position]);
            declare_local(init_captures[index].first,
                          LocalBinding{field, fields[field_position],
                                       capture_types[field_position - 1]});
        }
        emit_statement(*expression.children[1]);
        if (!builder_.insert_block()->terminated()) {
            if (ir::is_void(function_type->return_type)) {
                builder_.create_ret(nullptr);
            } else {
                builder_.create_ret(
                    module_->constant_undef(function_type->return_type));
            }
        }
        scopes_ = std::move(previous_scopes);
        current_function_ = previous_function;
        current_class_ = previous_class;
        builder_.set_insert_point(previous_block);

        lambda_metadata_[&expression] = LambdaInfo{closure_type, function_type};
        auto* storage = builder_.create_alloca(closure_type, 0, "lambda.closure");
        auto* zero = module_->constant_int(int_type(), 0);
        auto* fn_slot = builder_.create_gep(closure_type, storage, {zero, zero},
                                            false, "lambda.fn");
        fn_slot->type = module_->types().pointer_type(
            module_->types().pointer_type(byte_type));
        builder_.create_store(
            builder_.create(ir::Opcode::BitCast,
                            module_->types().pointer_type(byte_type), {function},
                            "lambda.fn.ptr"),
            fn_slot);
        for (std::size_t index = 0; index < captures.size(); ++index) {
            auto* field_index = module_->constant_int(
                int_type(), static_cast<std::int64_t>(index + 1));
            auto* field = builder_.create_gep(
                closure_type, storage, {zero, field_index}, false,
                "lambda.capture");
            field->type = module_->types().pointer_type(fields[index + 1]);
            const auto name = captures[index].starts_with("&")
                                  ? captures[index].substr(1)
                                  : captures[index];
            auto* local = lookup_local(name);
            if (!local || !local->type) {
                return {};
            }
            ir::Value* value = nullptr;
            if (captures[index].starts_with("&")) {
                value = local->address;
                if (value->type != fields[index + 1]) {
                    value = builder_.create(ir::Opcode::BitCast,
                                            fields[index + 1], {value},
                                            "lambda.capture.ref");
                }
            } else {
                value = builder_.create_load(
                    local->type, local->address, false, 0,
                    "lambda.capture.load");
                value = promote(value, local->type, fields[index + 1]);
            }
            builder_.create_store(value, field);
        }
        for (std::size_t index = 0; index < init_captures.size(); ++index) {
            const auto field_position = captures.size() + index + 1;
            auto* field_index = module_->constant_int(
                int_type(), static_cast<std::int64_t>(field_position));
            auto* field = builder_.create_gep(
                closure_type, storage, {zero, field_index}, false,
                "lambda.capture.init");
            field->type =
                module_->types().pointer_type(fields[field_position]);
            auto value = emit_expression(*init_captures[index].second);
            if (!value.value) {
                return {};
            }
            builder_.create_store(
                promote(value.value, value.type, fields[field_position]),
                field);
        }
        return {builder_.create(ir::Opcode::BitCast,
                                module_->types().pointer_type(closure_type),
                                {storage}, "lambda.closure.ptr"),
                module_->types().pointer_type(closure_type)};
    }
    const auto ir_function_type = map_type(lambda_type->element);
    if (!ir_function_type || ir_function_type->kind != ir::TypeKind::Function) {
        error(expression.location, "cannot map lambda type");
        return {};
    }
    auto* function = module_->add_function("__lambda_" +
                                               std::to_string(lambda_counter_++),
                                           ir_function_type, ir::Linkage::Internal,
                                           false);
    const auto* declarator = expression.children[0].get();
    std::size_t parameter_index = 0;
    if (declarator) {
        for (std::size_t index = 1; index < declarator->children.size(); ++index) {
            const auto* parameter = declarator->children[index].get();
            if (!parameter || parameter->kind != frontend::AstKind::ParameterDeclaration ||
                parameter_index >= lambda_type->element->parameters.size()) {
                continue;
            }
            function->add_parameter(
                parameter_ir_type(map_type(lambda_type->element->parameters[parameter_index])),
                parameter_name(*parameter, parameter_index));
            ++parameter_index;
        }
    }

    const auto previous_function = current_function_;
    const auto previous_class = current_class_;
    auto* previous_block = builder_.insert_block();
    const auto previous_scopes = std::move(scopes_);
    scopes_.clear();
    current_function_ = function;
    current_class_ = nullptr;
    auto* entry = function->add_block("entry");
    builder_.reset_value_names();
    builder_.set_insert_point(entry);
    push_scope();
    local_cleanups_.clear();
    for (std::size_t index = 0; index < function->parameters.size(); ++index) {
        const auto& parameter = function->parameters[index];
        auto* address = builder_.create_alloca(parameter->type, 0,
                                               parameter->name + ".addr");
        builder_.create_store(parameter.get(), address);
        declare_local(parameter->name, LocalBinding{address, parameter->type, nullptr});
    }
    emit_statement(*expression.children[1]);
    if (!builder_.insert_block()->terminated()) {
        builder_.create_ret(module_->constant_int(ir_function_type->return_type, 0));
    }
    scopes_ = std::move(previous_scopes);
    current_function_ = previous_function;
    current_class_ = previous_class;
    builder_.set_insert_point(previous_block);
    const auto pointer_type = module_->types().pointer_type(ir_function_type);
    return {builder_.create(ir::Opcode::BitCast, pointer_type, {function},
                            "lambda.ptr"),
            pointer_type};
}

CIRGen::Expression CIRGen::emit_condition_expression(
    const frontend::AstNode& expression) {
    if (expression.children.size() < 3 || !expression.children[0] ||
        !expression.children[1] || !expression.children[2]) {
        return {};
    }
    auto* condition = emit_condition(*expression.children[0]);
    std::function<frontend::TypePtr(const frontend::AstNode&)> infer_type =
        [&](const frontend::AstNode& node) -> frontend::TypePtr {
        switch (node.kind) {
        case frontend::AstKind::FloatLiteral: {
            auto type = std::make_shared<frontend::Type>();
            type->kind = !node.text.empty() &&
                                 (node.text.back() == 'f' ||
                                  node.text.back() == 'F')
                             ? frontend::TypeKind::Float
                             : frontend::TypeKind::Double;
            return type;
        }
        case frontend::AstKind::Identifier: {
            const auto* local = lookup_local(node.text);
            return local ? local->frontend_type : nullptr;
        }
        case frontend::AstKind::UnaryExpression:
            return node.children.empty() || !node.children.front()
                       ? nullptr
                       : infer_type(*node.children.front());
        case frontend::AstKind::BinaryExpression:
            if (node.children.size() >= 2 && node.children.front()) {
                auto left = infer_type(*node.children.front());
                if (left && left->kind == frontend::TypeKind::Double) {
                    return left;
                }
                if (node.children[1]) {
                    return infer_type(*node.children[1]);
                }
            }
            return nullptr;
        case frontend::AstKind::CastExpression:
            return node.children.empty() || !node.children.front()
                       ? nullptr
                       : sema_.type_from_type_name(*node.children.front());
        default:
            return nullptr;
        }
    };
    const auto then_frontend = infer_type(*expression.children[1]);
    const auto else_frontend = expression.children[2]
                                   ? infer_type(*expression.children[2])
                                   : nullptr;
    const auto pick_result = [&](frontend::TypePtr preferred,
                                 frontend::TypePtr fallback) {
        if (preferred && (preferred->kind == frontend::TypeKind::Double ||
                          preferred->kind == frontend::TypeKind::Float ||
                          preferred->kind == frontend::TypeKind::Pointer)) {
            return preferred;
        }
        if (fallback && (fallback->kind == frontend::TypeKind::Double ||
                         fallback->kind == frontend::TypeKind::Float ||
                         fallback->kind == frontend::TypeKind::Pointer)) {
            return fallback;
        }
        return preferred ? preferred : fallback;
    };
    // The analysed result type is authoritative: the local inference picks int
    // for a branch that names a variable, which would truncate an f64 or a
    // pointer into an i32 temporary.
    auto result_type = map_type(sema_.expression_type(expression));
    if (!result_type) {
        result_type = map_type(pick_result(then_frontend, else_frontend));
    }
    if (!result_type) {
        return {};
    }
    auto* temporary = builder_.create_alloca(result_type, 0, "cond.tmp");
    auto* then_block = current_function_->add_block("cond.then");
    auto* else_block = current_function_->add_block("cond.else");
    auto* end_block = current_function_->add_block("cond.end");
    emit_conditional_branch(condition, then_block, else_block);

    builder_.set_insert_point(then_block);
    auto then_value = emit_expression(*expression.children[1]);
    if (!then_value.value) {
        return {};
    }
    auto* converted_then = promote(then_value.value, then_value.type, result_type);
    builder_.create_store(converted_then, temporary);
    emit_branch(end_block);

    builder_.set_insert_point(else_block);
    auto else_value = emit_expression(*expression.children[2]);
    if (!else_value.value) {
        return {};
    }
    auto* converted_else = promote(else_value.value, else_value.type, result_type);
    builder_.create_store(converted_else, temporary);
    emit_branch(end_block);

    builder_.set_insert_point(end_block);
    return {builder_.create_load(result_type, temporary, false, 0, "cond.load"),
            result_type};
}

CIRGen::Expression CIRGen::emit_integer_literal(const frontend::AstNode& expression) {
    long long value = 0;
    if (!sema_.evaluate_constant_expression(expression, value)) {
        error(expression.location, "invalid integer literal");
        return {};
    }
    const auto type = type_for_integer_literal(expression.text, value);
    return {module_->constant_int(type, value), type};
}

ir::TypePtr CIRGen::type_for_integer_literal(std::string_view text, std::int64_t value) {
    bool is_unsigned = false;
    int long_count = 0;
    for (auto index = text.size(); index > 0; --index) {
        const char c = text[index - 1];
        if (c == 'u' || c == 'U') {
            is_unsigned = true;
        } else if (c == 'l' || c == 'L') {
            ++long_count;
        } else {
            break;
        }
    }
    const auto width = [&](frontend::TypeKind kind) -> std::uint32_t {
        switch (kind) {
        case frontend::TypeKind::Int:
            return profile_.int_bits;
        case frontend::TypeKind::Long:
            return profile_.long_bits;
        case frontend::TypeKind::LongLong:
            return profile_.long_long_bits;
        default:
            return 0;
        }
    };
    const auto magnitude = static_cast<std::uint64_t>(value);
    const auto fits = [&](frontend::TypeKind kind, bool candidate_unsigned) {
        const auto bits = width(kind);
        if (bits == 0) return false;
        if (bits >= 64) return true;
        const auto maximum =
            candidate_unsigned
                ? (static_cast<std::uint64_t>(1) << bits) - 1
                : (static_cast<std::uint64_t>(1) << (bits - 1)) - 1;
        return magnitude <= maximum;
    };
    frontend::TypeKind chosen = frontend::TypeKind::Int;
    if (long_count >= 2) {
        chosen = frontend::TypeKind::LongLong;
    } else if (long_count == 1) {
        chosen = frontend::TypeKind::Long;
        // C11 6.4.4.1: an `L` suffix names `long`, but the literal may still be
        // represented by `long long` when `long` cannot hold it.
        if (!fits(chosen, is_unsigned) &&
            fits(frontend::TypeKind::LongLong, is_unsigned)) {
            chosen = frontend::TypeKind::LongLong;
        }
    } else {
        // C11 6.4.4.1: an unsuffixed decimal literal takes the first of
        // `int`, `long`, `long long` that can represent it. Choosing `int`
        // unconditionally made `long x = 32768;` an i16 constant on kr216,
        // contradicting the target's own 32-bit `long`.
        chosen = frontend::TypeKind::LongLong;
        for (const auto candidate :
             {frontend::TypeKind::Int, frontend::TypeKind::Long,
              frontend::TypeKind::LongLong}) {
            if (fits(candidate, false)) {
                chosen = candidate;
                break;
            }
        }
    }
    return module_->types().int_type(width(chosen), !is_unsigned);
}

CIRGen::Expression CIRGen::emit_string_literal(const frontend::AstNode& expression) {
    auto bytes = decode_string(expression.text);
    bytes.push_back(0);
    const auto array_type = module_->types().array_type(
        module_->types().int_type(8, true), bytes.size());
    const auto name = ".str." + std::to_string(string_counter_++);
    auto* global = module_->add_global(name, array_type, true, 1);
    global->initializer = module_->constant_data_array(
        array_type, std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    auto* zero = module_->constant_int(int_type(), 0);
    auto* address = builder_.create_gep(array_type, global, {zero, zero}, false, "str");
    address->type = module_->types().pointer_type(module_->types().int_type(8, true));
    return {address, address->type};
}

std::string CIRGen::decode_string(std::string_view text) const {
    while (text.size() >= 2 && (text[0] == 'u' || text[0] == 'U' || text[0] == 'L')) {
        if (text[0] == 'u' && text.size() >= 3 && text[1] == '8') {
            text.remove_prefix(2);
        } else {
            text.remove_prefix(1);
        }
    }
    if (text.size() < 2 || text.front() != '"') {
        return {};
    }
    text.remove_prefix(1);
    if (!text.empty() && text.back() == '"') {
        text.remove_suffix(1);
    }
    std::string result;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '\\' || index + 1 >= text.size()) {
            result.push_back(text[index]);
            continue;
        }
        const char escape = text[++index];
        switch (escape) {
        case 'n':
            result.push_back('\n');
            break;
        case 't':
            result.push_back('\t');
            break;
        case 'r':
            result.push_back('\r');
            break;
        case 'a':
            result.push_back(7);
            break;
        case 'b':
            result.push_back(8);
            break;
        case 'f':
            result.push_back(12);
            break;
        case 'v':
            result.push_back(11);
            break;
        case '?':
            result.push_back('?');
            break;
        case '\\':
            result.push_back('\\');
            break;
        case '"':
            result.push_back('"');
            break;
        case '0':
            result.push_back('\0');
            break;
        case 'x': {
            unsigned value = 0;
            while (index + 1 < text.size()) {
                const char c = text[index + 1];
                unsigned digit = 0;
                if (c >= '0' && c <= '9') {
                    digit = static_cast<unsigned>(c - '0');
                } else if (c >= 'a' && c <= 'f') {
                    digit = static_cast<unsigned>(c - 'a' + 10);
                } else if (c >= 'A' && c <= 'F') {
                    digit = static_cast<unsigned>(c - 'A' + 10);
                } else {
                    break;
                }
                value = value * 16 + digit;
                ++index;
            }
            result.push_back(static_cast<char>(value));
            break;
        }
        default:
            result.push_back(escape);
            break;
        }
    }
    return result;
}

CIRGen::Expression CIRGen::emit_binary(const frontend::AstNode& expression) {
    if (expression.children.size() < 2 || !expression.children[0] ||
        !expression.children[1]) {
        return {};
    }
    // `typeid(a) == typeid(b)` is type equality. The compiler emits one
    // typeinfo object per class, so two type_info lvalues compare equal
    // exactly when they name the same type; compare their addresses.
    if ((expression.text == "==" || expression.text == "!=") &&
        (expression.children[0]->kind == frontend::AstKind::TypeIdExpression ||
         expression.children[1]->kind == frontend::AstKind::TypeIdExpression)) {
        auto info = sema_.type_named("std::type_info");
        if (!info) {
            info = sema_.type_named("type_info");
        }
        const auto info_type = info ? map_type(info) : nullptr;
        if (info_type) {
            const auto address_of = [&](const frontend::AstNode& node) -> Expression {
                if (node.kind == frontend::AstKind::TypeIdExpression) {
                    return emit_typeid_reference(node);
                }
                auto lvalue = emit_lvalue(node);
                if (!lvalue.value || !lvalue.type || lvalue.type != info_type) {
                    return {};
                }
                return {lvalue.value, info_type};
            };
            auto left = address_of(*expression.children[0]);
            auto right = address_of(*expression.children[1]);
            if (left.value && right.value) {
                auto* condition = builder_.create_icmp(
                    expression.text == "==" ? ir::ICmpPredicate::Eq
                                            : ir::ICmpPredicate::Ne,
                    left.value, right.value, "typeid.compare");
                return {condition, module_->types().bool_type()};
            }
        }
    }
    // An operand that is not a name still resolves a member operator through
    // the record its address names, so *this == other calls the member.
    const auto left_kind = expression.children[0]->kind;
    bool left_names_object =
        left_kind == frontend::AstKind::MemberExpression ||
        (left_kind == frontend::AstKind::UnaryExpression &&
         expression.children[0]->text == "*");
    if (!left_names_object && left_kind == frontend::AstKind::Identifier) {
        // A name bound to a record, or to a reference to one, is an object too:
        // const type_info& a = ...; a != b must reach the member operator
        // rather than comparing two records with icmp.
        const auto* local = lookup_local(expression.children[0]->text);
        if (local != nullptr && local->frontend_type) {
            const auto& bound = *local->frontend_type;
            left_names_object =
                bound.kind == frontend::TypeKind::Record ||
                (bound.element != nullptr &&
                 bound.element->kind == frontend::TypeKind::Record);
        }
    }
    if (left_names_object) {
        const auto method_name = "operator" + expression.text;
        auto self = emit_lvalue(*expression.children[0]);
        if (self.value && self.type) {
            const auto record = ir_records_.find(self.type);
            if (record != ir_records_.end() && record->second) {
                const auto methods = class_methods_.find(record->second->tag);
                if (methods != class_methods_.end()) {
                    const auto found = methods->second.find(method_name);
                    if (found != methods->second.end()) {
                        auto result = binary_operator_call(
                            found->second, self, *expression.children[1]);
                        if (result.value) {
                            return result;
                        }
                    }
                }
            }
        }
    }
    if (expression.children[0]->kind == frontend::AstKind::Identifier) {
        if (auto* local = lookup_local(expression.children[0]->text)) {
            if (local->frontend_type &&
                local->frontend_type->kind == frontend::TypeKind::Record &&
                local->frontend_type->record) {
                const auto method_name = "operator" + expression.text;
                ir::Function* method_function = nullptr;
                for (auto* record = local->frontend_type->record.get(); record != nullptr;
                     record = record->base && record->base->record
                                  ? record->base->record.get()
                                  : nullptr) {
                    const auto methods = class_methods_.find(record->tag);
                    if (methods == class_methods_.end()) {
                        continue;
                    }
                    const auto found = methods->second.find(method_name);
                    if (found != methods->second.end()) {
                        method_function = found->second;
                        break;
                    }
                }
                if (method_function) {
                    auto self = emit_lvalue(*expression.children[0]);
                    if (self.value) {
                        return binary_operator_call(method_function, self,
                                                   *expression.children[1]);
                    }
                }
                if (auto* free_function = lookup_function(method_name)) {
                    const auto ast_type = function_ast_types_.find(free_function);
                    const bool left_reference =
                        ast_type != function_ast_types_.end() && ast_type->second &&
                        !ast_type->second->parameters.empty() &&
                        ast_type->second->parameters[0] &&
                        ast_type->second->parameters[0]->is_reference;
                    const bool right_reference =
                        ast_type != function_ast_types_.end() && ast_type->second &&
                        ast_type->second->parameters.size() > 1 &&
                        ast_type->second->parameters[1] &&
                        ast_type->second->parameters[1]->is_reference;
                    auto left_argument = left_reference
                                             ? emit_lvalue(*expression.children[0])
                                             : emit_expression(*expression.children[0]);
                    auto right_argument = right_reference
                                              ? emit_lvalue(*expression.children[1])
                                              : emit_expression(*expression.children[1]);
                    if (!left_argument.value || !right_argument.value) {
                        return {};
                    }
                    if (left_reference && left_argument.value->type) {
                        left_argument.type = left_argument.value->type;
                    }
                    if (right_reference && right_argument.value->type) {
                        right_argument.type = right_argument.value->type;
                    }
                    std::vector<ir::Value*> arguments{
                        promote(left_argument.value, left_argument.type,
                                free_function->type->parameters[0]),
                        promote(right_argument.value, right_argument.type,
                                free_function->type->parameters[1])};
                    auto* call = create_call_instruction(
                        free_function, std::move(arguments),
                        free_function->type->return_type, "operator.call");
                    return {call, free_function->type->return_type};
                }
            }
        }
    }
    // `&&` and `||` short-circuit, so the right operand must not be lowered
    // here: it is emitted inside the branch its own block below, so a side
    // effect it carries runs only when the left operand does not decide.
    const bool short_circuit =
        expression.text == "&&" || expression.text == "||";
    auto left = emit_expression(*expression.children[0]);
    auto right = short_circuit ? Expression{} : emit_expression(*expression.children[1]);
    if (!left.value || (!short_circuit && !right.value)) {
        return {};
    }
    const bool left_record =
        left.type && left.type->kind == ir::TypeKind::Struct;
    const bool right_record =
        right.type && right.type->kind == ir::TypeKind::Struct;
    if (!left_record && !right_record &&
        (ir::is_float(left.type) || ir::is_float(right.type))) {
        ir::TypePtr common = left.type;
        if (!ir::is_float(common) || (right.type && right.type->bits > common->bits)) {
            common = right.type;
        }
        left.value = promote(left.value, left.type, common);
        left.type = common;
        right.value = promote(right.value, right.type, common);
        right.type = common;
    }
    if (ir::is_integer(left.type) && ir::is_integer(right.type)) {
        ir::TypePtr common = left.type;
        if (left.type->bits < static_cast<unsigned>(profile_.int_bits) ||
            right.type->bits < static_cast<unsigned>(profile_.int_bits)) {
            common = int_type();
        } else if (right.type->bits > left.type->bits) {
            common = right.type;
        } else if (right.type->bits == left.type->bits &&
                   (left.type->is_signed != right.type->is_signed)) {
            common = module_->types().int_type(left.type->bits, false);
        }
        left.value = promote(left.value, left.type, common);
        left.type = common;
        right.value = promote(right.value, right.type, common);
        right.type = common;
    }
    const auto& operation = expression.text;
    if ((operation == "==" || operation == "!=") && ir::is_pointer(left.type) &&
        ir::is_integer(right.type)) {
        right.value = promote(right.value, right.type, left.type);
        right.type = left.type;
    } else if ((operation == "==" || operation == "!=") && ir::is_integer(left.type) &&
               ir::is_pointer(right.type)) {
        left.value = promote(left.value, left.type, right.type);
        left.type = right.type;
    } else if ((operation == "==" || operation == "!=") &&
               ir::is_pointer(left.type) && ir::is_pointer(right.type) &&
               left.type != right.type && left.type->element &&
               right.type->element) {
        if (left.type->element->kind == ir::TypeKind::Void &&
            right.type->element->kind != ir::TypeKind::Void) {
            left.value = builder_.create(ir::Opcode::BitCast, right.type,
                                         {left.value}, "pointer.compare");
            left.type = right.type;
        } else {
            right.value = builder_.create(ir::Opcode::BitCast, left.type,
                                          {right.value}, "pointer.compare");
            right.type = left.type;
        }
    }
    if (operation == "&&" || operation == "||") {
        std::function<ir::Value *(const Expression &, const frontend::AstNode &)>
            truth;
        truth = [&](const Expression& value,
                     const frontend::AstNode& node) -> ir::Value* {
            if (!value.value || !value.type) {
                return nullptr;
            }
            if (value.type->kind == ir::TypeKind::Struct) {
                const auto record = ir_records_.find(value.type);
                if (record == ir_records_.end() || !record->second) {
                    return nullptr;
                }
                const auto methods = class_methods_.find(record->second->tag);
                if (methods == class_methods_.end()) {
                    return nullptr;
                }
                auto method = methods->second.find("operator bool");
                if (method == methods->second.end()) {
                    method = methods->second.find("operator int");
                }
                if (method == methods->second.end()) {
                    return nullptr;
                }
                Expression self;
                if (node.kind == frontend::AstKind::CallExpression) {
                    auto record_value = emit_expression(node);
                    if (record_value.value && record_value.type &&
                        record_value.type->kind == ir::TypeKind::Struct) {
                        auto* storage = builder_.create_alloca(
                            record_value.type, 0, "logical.temp");
                        builder_.create_store(record_value.value, storage);
                        self.value = storage;
                        self.type = record_value.type;
                    }
                } else {
                    self = emit_lvalue(node);
                }
                if (!self.value) {
                    return nullptr;
                }
                if (self.value->type !=
                    method->second->type->parameters[0]) {
                    self.value = builder_.create(
                        ir::Opcode::BitCast,
                        method->second->type->parameters[0],
                        {self.value}, "logical.this");
                }
                auto* call = create_call_instruction(
                    method->second, {self.value},
                    method->second->type->return_type, "logical.conversion");
                const Expression converted{call,
                                           method->second->type->return_type};
                return truth(converted, node);
            }
            if (ir::is_integer(value.type) && value.type->bits == 1) {
                return value.value;
            }
            if (ir::is_pointer(value.type)) {
                return builder_.create_icmp(
                    ir::ICmpPredicate::Ne, value.value,
                    module_->constant_null(value.type), "tobool");
            }
            if (ir::is_float(value.type)) {
                return builder_.create_fcmp(
                    ir::FCmpPredicate::One, value.value,
                    module_->constant_float(value.type, 0.0), "tobool");
            }
            return builder_.create_icmp(
                ir::ICmpPredicate::Ne, value.value,
                module_->constant_int(value.type, 0), "tobool");
        };
        auto* left_bool = truth(left, *expression.children[0]);
        if (!left_bool) {
            return {};
        }
        const bool is_and = operation == "&&";
        const auto bool_type = module_->types().bool_type();
        // The result crosses the branch in a slot: the target backends lower
        // allocas and loads and have no phi.
        auto* slot = builder_.create_alloca(bool_type, 0, "logic.slot");
        builder_.create_store(
            module_->constant_int(bool_type, is_and ? 0 : 1), slot);
        auto* right_block = current_function_->add_block("logic.rhs");
        auto* merge_block = current_function_->add_block("logic.end");
        emit_conditional_branch(left_bool, is_and ? right_block : merge_block,
                                is_and ? merge_block : right_block);
        // The right operand is lowered inside its own block, so its side
        // effects run only when the left operand does not decide the result.
        builder_.set_insert_point(right_block);
        auto right_value = emit_expression(*expression.children[1]);
        if (!right_value.value) {
            return {};
        }
        auto* right_bool = truth(right_value, *expression.children[1]);
        if (!right_bool) {
            return {};
        }
        builder_.create_store(right_bool, slot);
        emit_branch(merge_block);
        builder_.set_insert_point(merge_block);
        auto* merged = builder_.create_load(bool_type, slot, false, 0, "logic");
        return {merged, bool_type};
    }
    if (operation == "==" || operation == "!=" || operation == "<" ||
        operation == ">" || operation == "<=" || operation == ">=") {
        auto convert_record = [&](Expression& value,
                                const frontend::AstNode& node,
                                ir::TypePtr target) {
            if (!value.value || !value.type ||
                value.type->kind != ir::TypeKind::Struct) {
                return;
            }
            const auto record = ir_records_.find(value.type);
            if (record == ir_records_.end() || !record->second) {
                return;
            }
            const auto methods = class_methods_.find(record->second->tag);
            if (methods == class_methods_.end()) {
                return;
            }
            const char* conversion = nullptr;
            if (ir::is_integer(target)) {
                conversion =
                    target->bits == 1 ? "operator bool" : "operator int";
            } else {
                conversion =
                    target->bits == 64 ? "operator double" : "operator float";
            }
            const auto method = methods->second.find(conversion);
            if (method == methods->second.end()) {
                return;
            }
            auto* storage = builder_.create_alloca(value.type, 0,
                                                       "compare.temp");
            builder_.create_store(value.value, storage);
            auto* self = storage;
            if (self->type != method->second->type->parameters[0]) {
                self = builder_.create(
                    ir::Opcode::BitCast,
                    method->second->type->parameters[0], {self},
                    "compare.this");
            }
            auto* call = create_call_instruction(
                method->second, {self},
                method->second->type->return_type, "compare.conversion");
            value.value = call;
            value.type = method->second->type->return_type;
            static_cast<void>(node);
        };
        if ((ir::is_integer(left.type) || ir::is_float(left.type)) &&
            right.type && right.type->kind == ir::TypeKind::Struct) {
            convert_record(right, *expression.children[1], left.type);
        } else if ((ir::is_integer(right.type) ||
                    ir::is_float(right.type)) &&
                   left.type && left.type->kind == ir::TypeKind::Struct) {
            convert_record(left, *expression.children[0], right.type);
        }
    }
    if (operation == "==" || operation == "!=" || operation == "<" || operation == ">" ||
        operation == "<=" || operation == ">=") {
        if (ir::is_float(left.type) && ir::is_float(right.type)) {
            ir::FCmpPredicate predicate = ir::FCmpPredicate::Oeq;
            if (operation == "!=") {
                predicate = ir::FCmpPredicate::One;
            } else if (operation == "<") {
                predicate = ir::FCmpPredicate::Olt;
            } else if (operation == ">") {
                predicate = ir::FCmpPredicate::Ogt;
            } else if (operation == "<=") {
                predicate = ir::FCmpPredicate::Ole;
            } else if (operation == ">=") {
                predicate = ir::FCmpPredicate::Oge;
            }
            auto* value = builder_.create_fcmp(predicate, left.value, right.value, "cmp");
            return {value, value->type};
        }
        ir::ICmpPredicate predicate = ir::ICmpPredicate::Eq;
        const bool is_signed = left.type && left.type->is_signed;
        if (operation == "!=") {
            predicate = ir::ICmpPredicate::Ne;
        } else if (operation == "<") {
            predicate = is_signed ? ir::ICmpPredicate::Slt : ir::ICmpPredicate::Ult;
        } else if (operation == ">") {
            predicate = is_signed ? ir::ICmpPredicate::Sgt : ir::ICmpPredicate::Ugt;
        } else if (operation == "<=") {
            predicate = is_signed ? ir::ICmpPredicate::Sle : ir::ICmpPredicate::Ule;
        } else if (operation == ">=") {
            predicate = is_signed ? ir::ICmpPredicate::Sge : ir::ICmpPredicate::Uge;
        }
        auto* value = builder_.create_icmp(predicate, left.value, right.value, "cmp");
        return {value, value->type};
    }
    const bool arithmetic = operation == "+" || operation == "-" ||
                          operation == "*" || operation == "/" ||
                          operation == "%" || operation == "<<" ||
                          operation == ">>" || operation == "&" ||
                          operation == "|" || operation == "^";
    if (arithmetic && left_record != right_record) {
        Expression& converted = left_record ? left : right;
        const frontend::AstNode& node =
            left_record ? *expression.children[0] : *expression.children[1];
        const ir::TypePtr target = left_record ? right.type : left.type;
        if (target && (ir::is_integer(target) || ir::is_float(target))) {
            const auto record = ir_records_.find(converted.type);
            if (record != ir_records_.end() && record->second) {
                const auto methods =
                    class_methods_.find(record->second->tag);
                if (methods != class_methods_.end()) {
                    const char* conversion = nullptr;
                    if (ir::is_integer(target)) {
                        conversion = target->bits == 1 ? "operator bool"
                                                      : "operator int";
                    } else {
                        conversion = target->bits == 64
                                         ? "operator double"
                                         : "operator float";
                    }
                    const auto method =
                        methods->second.find(conversion);
                    if (method != methods->second.end()) {
                        auto* storage = builder_.create_alloca(
                            converted.type, 0, "arith.temp");
                        builder_.create_store(converted.value, storage);
                        auto* self = storage;
                        if (self->type !=
                            method->second->type->parameters[0]) {
                            self = builder_.create(
                                ir::Opcode::BitCast,
                                method->second->type->parameters[0],
                                {self}, "arith.this");
                        }
                        auto* call = create_call_instruction(
                            method->second, {self},
                            method->second->type->return_type,
                            "arith.conversion");
                        converted.value = call;
                        converted.type =
                            method->second->type->return_type;
                        static_cast<void>(node);
                    }
                }
            }
        }
    }
    if (operation == "-" && ir::is_pointer(left.type) &&
        ir::is_pointer(right.type)) {
        auto* left_integer = builder_.create(
            ir::Opcode::PtrToInt, int_type(), {left.value}, "ptrdiff.left");
        auto* right_integer = builder_.create(
            ir::Opcode::PtrToInt, int_type(), {right.value}, "ptrdiff.right");
        auto* difference = builder_.create_sub(left_integer, right_integer,
                                               "ptrdiff.bytes");
        const auto element_size =
            left.type->element ? ir_size_of(left.type->element) : 0;
        if (element_size > 1) {
            auto* divisor = module_->constant_int(
                int_type(), static_cast<std::int64_t>(element_size));
            difference = builder_.create_sdiv(difference, divisor,
                                             "ptrdiff.elements");
        }
        return {difference, int_type()};
    }
    if ((operation == "+" || operation == "-") && ir::is_pointer(left.type)) {
        auto* zero = module_->constant_int(int_type(), 0);
        const bool subtract = operation == "-";
        // The offset is an element count, so it takes the index type whatever
        // the operand was; without this an unsigned `sizeof` operand makes the
        // subtraction's operand types disagree.
        auto* offset = promote(right.value, right.type, int_type());
        auto* index = subtract ? builder_.create_sub(zero, offset, "neg") : offset;
        auto* value = builder_.create_gep(left.type->element, left.value, {index}, false,
                                          "ptradd");
        value->type = left.type;
        return {value, left.type};
    }
    ir::Opcode opcode = ir::Opcode::Add;
    if (operation == "-") {
        opcode = ir::Opcode::Sub;
    } else if (operation == "*") {
        opcode = ir::Opcode::Mul;
    } else if (operation == "/") {
        opcode = (left.type && left.type->is_signed) ? ir::Opcode::SDiv : ir::Opcode::UDiv;
    } else if (operation == "%") {
        opcode = (left.type && left.type->is_signed) ? ir::Opcode::SRem : ir::Opcode::URem;
    } else if (operation == "<<") {
        opcode = ir::Opcode::Shl;
    } else if (operation == ">>") {
        opcode = (left.type && left.type->is_signed) ? ir::Opcode::AShr : ir::Opcode::LShr;
    } else if (operation == "&") {
        opcode = ir::Opcode::And;
    } else if (operation == "|") {
        opcode = ir::Opcode::Or;
    } else if (operation == "^") {
        opcode = ir::Opcode::Xor;
    }
    if (ir::is_float(left.type) || ir::is_float(right.type)) {
        if (operation == "+") {
            opcode = ir::Opcode::FAdd;
        } else if (operation == "-") {
            opcode = ir::Opcode::FSub;
        } else if (operation == "*") {
            opcode = ir::Opcode::FMul;
        } else if (operation == "/") {
            opcode = ir::Opcode::FDiv;
        }
    }
    auto* value = builder_.create(opcode, left.type, {left.value, right.value}, "arith");
    return {value, left.type};
}


}  // namespace krc::irgen
