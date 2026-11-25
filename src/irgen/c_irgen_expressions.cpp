#include "irgen/c_irgen_internal.h"

namespace krc::irgen {

namespace {

// The pointee qualifier of a declared pointer type, if it has one.
bool pointer_to_volatile(const frontend::TypePtr& type) {
    return type && type->kind == frontend::TypeKind::Pointer && type->element &&
           type->element->is_volatile;
}

}  // namespace

bool CIRGen::dereference_is_volatile(const frontend::AstNode& operand) {
    // Pointer arithmetic keeps the pointee, so `*(p + 1)` is as volatile as `*p` and
    // `*(1 + p)` is the same expression. IRGen has no general expression typing -- the
    // AST carries no types, `ir::Type` has no qualifier, and the qualifier survives only
    // on a declared binding -- so the rule stops at the forms whose pointee is a
    // declared type. A call result or a cast answers false, which loses the marking
    // rather than inventing one: no pass acts on a load's volatility today, whereas a
    // false positive would suppress optimisation of a non-volatile access.
    if (operand.kind == frontend::AstKind::BinaryExpression && operand.children.size() == 2 &&
        (operand.text == "+" || operand.text == "-")) {
        return dereference_is_volatile(*operand.children[0]) ||
               dereference_is_volatile(*operand.children[1]);
    }
    if (operand.kind != frontend::AstKind::Identifier) {
        return false;
    }
    if (auto* local = lookup_local(operand.text)) {
        return pointer_to_volatile(local->frontend_type);
    }
    const auto global = globals_.find(operand.text);
    if (global == globals_.end()) {
        return false;
    }
    const auto frontend = global_frontend_types_.find(global->second);
    return frontend != global_frontend_types_.end() &&
           pointer_to_volatile(frontend->second);
}

CIRGen::Expression CIRGen::emit_unary(const frontend::AstNode& expression) {
    if (expression.children.empty() || !expression.children.front()) {
        return {};
    }
    if (expression.text == "&") {
        auto address = emit_lvalue(*expression.children.front());
        if (!address.value) {
            return {};
        }
        auto* type = module_->types().pointer_type(address.type);
        return {address.value, type};
    }
    if (expression.text == "*") {
        auto pointer = emit_expression(*expression.children.front());
        if (!pointer.value || !pointer.type || pointer.type->kind != ir::TypeKind::Pointer) {
            // A class may overload dereference, so *owner calls the member.
            if (auto* overload = dereference_overload(pointer)) {
                auto object = emit_lvalue(*expression.children.front());
                return unary_operator_call(overload, object, false);
            }
            error(expression.location, "dereference of a non-pointer");
            return {};
        }
        const auto type = pointer.type->element;
        // A dereference through a volatile-qualified pointer is a volatile access.
        return {builder_.create_load(
                    type, pointer.value,
                    dereference_is_volatile(*expression.children.front()), 0,
                    "deref"),
                type};
    }
    if (expression.text == "placement_new") {
        if (expression.children.size() < 2 || !expression.children[0] ||
            !expression.children[1]) {
            return {};
        }
        const auto frontend_type = sema_.type_from_type_name(*expression.children[1]);
        const auto type = frontend_type ? map_type(frontend_type) : nullptr;
        if (!type) {
            error(expression.location, "placement new requires a type");
            return {};
        }
        auto placement = emit_expression(*expression.children[0]);
        if (!placement.value) {
            return {};
        }
        auto* pointer = builder_.create(ir::Opcode::BitCast,
                                        module_->types().pointer_type(type),
                                        {placement.value}, "placement.new");
        emit_vtable_init(pointer, type);
        if (type->kind == ir::TypeKind::Struct) {
            const auto record = ir_records_.find(type);
            if (record != ir_records_.end()) {
                std::size_t argument_count = 0;
                if (expression.children.size() > 2 &&
                    expression.children[2] &&
                    expression.children[2]->kind ==
                        frontend::AstKind::ExpressionStatement) {
                    argument_count =
                        expression.children[2]->children.size();
                }
                auto* constructor = select_constructor(
                    record->second->tag, argument_count);
                // A constructor that exists only as a class template pattern's
                // declaration has no body to call (ADR 0032), and a placement-new'd
                // record with no members has nothing to construct. Emitting the call left
                // `new (buffer) std::tuple<>()` bound to an undefined
                // `tuple_ctor_0_value`; the local-initialisation path carries the same
                // guard for the same reason.
                if (constructor && constructor->declaration &&
                    record->second->members.empty()) {
                    constructor = nullptr;
                }
                if (constructor) {
                    std::vector<ir::Value*> arguments{pointer};
                    if (expression.children.size() > 2 &&
                        expression.children[2] &&
                        expression.children[2]->kind ==
                            frontend::AstKind::ExpressionStatement) {
                        const auto& argument_nodes = expression.children[2]->children;
                        for (std::size_t index = 0; index < argument_nodes.size();
                             ++index) {
                            if (!argument_nodes[index]) {
                                continue;
                            }
                            auto argument = emit_expression(*argument_nodes[index]);
                            const auto parameter_index = index + 1;
                            if (parameter_index < constructor->type->parameters.size()) {
                                argument.value = promote(
                                    argument.value, argument.type,
                                    constructor->type->parameters[parameter_index]);
                            }
                            arguments.push_back(argument.value);
                        }
                    }
                    create_call_instruction(constructor, std::move(arguments), nullptr,
                                         "placement.ctor");
                }
            }
        }
        return {pointer, pointer->type};
    }
    if (expression.text == "new") {
        const auto frontend_type =
            expression.children.empty() || !expression.children.front()
                ? nullptr
                : sema_.type_from_type_name(*expression.children.front());
        const auto mapped_type = frontend_type ? map_type(frontend_type) : nullptr;
        if (!mapped_type) {
            error(expression.location, "new requires a type");
            return {};
        }
        const bool type_is_array = mapped_type->kind == ir::TypeKind::Array;
        const auto type = type_is_array ? mapped_type->element : mapped_type;
        auto* malloc_function = functions_.find("malloc") != functions_.end()
                                    ? functions_["malloc"]
                                    : nullptr;
        if (!malloc_function) {
            auto* function_type = module_->types().function_type(
                module_->types().pointer_type(module_->types().void_type()),
                {size_type()}, false);
            malloc_function = module_->add_function("malloc", function_type,
                                                   ir::Linkage::External, true);
            malloc_function->add_parameter(size_type(), "size");
            functions_["malloc"] = malloc_function;
        }
        const bool has_argument_list =
            !type_is_array && expression.children.size() > 1 &&
            expression.children[1] &&
            expression.children[1]->kind ==
                frontend::AstKind::ExpressionStatement;
        const frontend::AstNode* array_initializer = nullptr;
        if (type_is_array && expression.children.size() > 1 &&
            expression.children[1] &&
            expression.children[1]->kind ==
                frontend::AstKind::ExpressionStatement) {
            array_initializer = expression.children[1].get();
        } else if (!type_is_array && expression.children.size() > 2 &&
                   expression.children[2] &&
                   expression.children[2]->kind ==
                       frontend::AstKind::ExpressionStatement) {
            array_initializer = expression.children[2].get();
        }
        const bool array =
            type_is_array ||
            (expression.children.size() > 1 && expression.children[1] &&
             !has_argument_list);
        ir::Value* count_value = nullptr;
        if (type_is_array) {
            count_value = module_->constant_int(
                size_type(), static_cast<std::int64_t>(mapped_type->count));
        }
        std::vector<const frontend::AstNode*> constructor_arguments;
        if (has_argument_list) {
            for (const auto& argument : expression.children[1]->children) {
                constructor_arguments.push_back(argument.get());
            }
        }
        ir::Value* size = module_->constant_int(size_type(), ir_size_of(type));
        if (array) {
            if (!count_value) {
                auto count = emit_expression(*expression.children[1]);
                count_value = promote(count.value, count.type, size_type());
            }
            size = builder_.create_add(
                builder_.create_mul(module_->constant_int(size_type(),
                                                          ir_size_of(type)),
                                    count_value, "new.size"),
                module_->constant_int(size_type(), 4), "new.cookie.size");
        }
        auto* call = create_call_instruction(malloc_function, {size},
                                          module_->types().pointer_type(
                                              module_->types().void_type()),
                                          "new.raw");
        auto* raw = builder_.create(
            ir::Opcode::BitCast, module_->types().pointer_type(module_->types().int_type(8)),
            {call}, "new.raw.bytes");
        ir::Value* pointer = nullptr;
        if (array) {
            auto* count_pointer = builder_.create(
                ir::Opcode::BitCast, module_->types().pointer_type(size_type()),
                {raw}, "new.count.ptr");
            builder_.create_store(count_value, count_pointer);
            auto* offset = builder_.create_gep(
                module_->types().int_type(8), raw,
                {module_->constant_int(size_type(), 4)}, false, "new.data");
            pointer = builder_.create(ir::Opcode::BitCast,
                                      module_->types().pointer_type(type), {offset},
                                      "new");
        } else {
            pointer = builder_.create(ir::Opcode::BitCast,
                                      module_->types().pointer_type(type), {call}, "new");
        }
        if (array && array_initializer) {
            auto* memset_function =
                functions_.find("memset") != functions_.end()
                    ? functions_["memset"]
                    : nullptr;
            if (!memset_function) {
                auto* function_type = module_->types().function_type(
                    module_->types().pointer_type(
                        module_->types().void_type()),
                    {module_->types().pointer_type(
                         module_->types().void_type()),
                     int_type(), size_type()},
                    false);
                memset_function = module_->add_function(
                    "memset", function_type, ir::Linkage::External, true);
                memset_function->add_parameter(
                    module_->types().pointer_type(
                        module_->types().void_type()),
                    "destination");
                memset_function->add_parameter(int_type(), "value");
                memset_function->add_parameter(size_type(), "size");
                functions_["memset"] = memset_function;
            }
            auto* bytes = builder_.create_mul(
                module_->constant_int(size_type(), ir_size_of(type)),
                count_value, "new.init.size");
            auto* destination = builder_.create(
                ir::Opcode::BitCast,
                module_->types().pointer_type(
                    module_->types().void_type()),
                {pointer}, "new.init.bytes");
            create_call_instruction(memset_function,
                                  {destination,
                                   module_->constant_int(int_type(), 0),
                                   bytes},
                                  memset_function->type->return_type,
                                  "new.init.clear");
            const auto& initializers = array_initializer->children;
            for (std::size_t index = 0; index < initializers.size(); ++index) {
                if (!initializers[index]) {
                    continue;
                }
                // Two indices: the first steps past the array type itself, the
                // second selects an element. With one index the stride is the whole
                // array, so element 1 landed at base + the array size and every
                // initialiser past the first wrote outside it.
                auto* element_pointer = builder_.create_gep(
                    type, pointer,
                    {module_->constant_int(size_type(), 0),
                     module_->constant_int(size_type(),
                                           static_cast<std::int64_t>(index))},
                    false, "new.element");
                element_pointer->type = module_->types().pointer_type(type);
                if (initializers[index]->kind ==
                    frontend::AstKind::InitializerList) {
                    if (type->kind == ir::TypeKind::Struct) {
                        emit_initializer(*initializers[index],
                                         element_pointer, type);
                    }
                    continue;
                }
                auto element = emit_expression(*initializers[index]);
                builder_.create_store(
                    promote(element.value, element.type, type),
                    element_pointer);
            }
        }
        if (!array && !constructor_arguments.empty() &&
            (ir::is_integer(type) || ir::is_float(type)) &&
            constructor_arguments.front()) {
            auto value = emit_expression(*constructor_arguments.front());
            if (value.value) {
                builder_.create_store(
                    promote(value.value, value.type, type), pointer);
            }
        }
        if (!array && has_argument_list && constructor_arguments.empty() &&
            (ir::is_integer(type) || ir::is_float(type))) {
            if (ir::is_integer(type)) {
                builder_.create_store(module_->constant_int(type, 0),
                                      pointer);
            } else {
                builder_.create_store(module_->constant_float(type, 0.0),
                                      pointer);
            }
        }
        if (type->kind == ir::TypeKind::Struct) {
            if (!array) {
                emit_vtable_init(pointer, type);
            }
            const auto record = ir_records_.find(type);
            if (record != ir_records_.end()) {
                auto* constructor = select_constructor(
                    record->second->tag, constructor_arguments.size());
                if (!constructor && constructor_arguments.empty()) {
                    constructor = ensure_default_constructor(record->second);
                }
                const bool needs_vtable =
                    record->second && record->second->polymorphic;
                const bool has_defaults = std::any_of(
                    record->second->members.begin(),
                    record->second->members.end(),
                    [](const frontend::RecordMember& member) {
                        return member.default_initializer != nullptr;
                    });
                bool has_member_ctors = false;
                for (const auto& member : record->second->members) {
                    if (member.type &&
                        member.type->kind == frontend::TypeKind::Record &&
                        member.type->record &&
                        select_constructor(member.type->record->tag, 0)) {
                        has_member_ctors = true;
                    }
                }
                bool has_base_ctor = false;
                if (record->second->base &&
                    record->second->base->kind ==
                        frontend::TypeKind::Record &&
                    record->second->base->record &&
                    select_constructor(
                        record->second->base->record->tag, 0)) {
                    has_base_ctor = true;
                }
                const auto emit_defaults = [&](ir::Value* target) {
                    if (record->second->polymorphic) {
                        return;
                    }
                    if (record->second->base &&
                        record->second->base->kind ==
                            frontend::TypeKind::Record &&
                        record->second->base->record) {
                        auto* base_constructor = select_constructor(
                            record->second->base->record->tag, 0);
                        if (base_constructor) {
                            auto* base_this = target;
                            if (base_this->type !=
                                base_constructor->type->parameters[0]) {
                                base_this = builder_.create(
                                    ir::Opcode::BitCast,
                                    base_constructor->type->parameters[0],
                                    {base_this}, "new.base.this");
                            }
                            create_call_instruction(
                                base_constructor, {base_this}, nullptr,
                                "new.base.ctor");
                        }
                    }
                    const auto field_offset =
                        type->fields.size() -
                        record->second->members.size();
                    for (std::size_t member_index = 0;
                         member_index < record->second->members.size() &&
                         member_index < type->fields.size();
                         ++member_index) {
                        const auto& member =
                            record->second->members[member_index];
                        const auto position = field_offset + member_index;
                        if (position >= type->fields.size()) {
                            continue;
                        }
                        auto* zero = module_->constant_int(int_type(), 0);
                        auto* field_index = module_->constant_int(
                            int_type(),
                            static_cast<std::int64_t>(position));
                        auto* field = builder_.create_gep(
                            type, target, {zero, field_index}, false,
                            "new.default");
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
                                {self}, "new.member.this");
                        }
                        create_call_instruction(member_constructor, {self},
                                             nullptr, "new.member.ctor");
                    }
                };
                if (array && count_value &&
                    (constructor || needs_vtable || has_defaults ||
                     has_member_ctors || has_base_ctor)) {
                    auto* index = builder_.create_alloca(size_type(), 0, "new.index");
                    builder_.create_store(module_->constant_int(size_type(), 0), index);
                    auto* condition_block = current_function_->add_block("new.cond");
                    auto* body_block = current_function_->add_block("new.body");
                    auto* increment_block = current_function_->add_block("new.inc");
                    auto* end_block = current_function_->add_block("new.end");
                    emit_branch(condition_block);
                    builder_.set_insert_point(condition_block);
                    auto* current =
                        builder_.create_load(size_type(), index, false, 0, "new.i");
                    auto* condition = builder_.create_icmp(ir::ICmpPredicate::Ult, current,
                                                           count_value, "new.cmp");
                    emit_conditional_branch(condition, body_block, end_block);
                    builder_.set_insert_point(body_block);
                    auto* element = builder_.create_gep(type, pointer, {current}, false,
                                                        "new.element");
                    element->type = module_->types().pointer_type(type);
                    auto* self = element;
                    emit_vtable_init(self, type);
                    if (constructor) {
                        if (self->type != constructor->type->parameters[0]) {
                            self = builder_.create(ir::Opcode::BitCast,
                                                   constructor->type->parameters[0],
                                                   {self}, "new.this");
                        }
                        create_call_instruction(constructor, {self}, nullptr,
                                             "new.ctor");
                    } else if (has_defaults || has_member_ctors ||
                               has_base_ctor) {
                        emit_defaults(self);
                    }
                    emit_branch(increment_block);
                    builder_.set_insert_point(increment_block);
                    auto* old = builder_.create_load(size_type(), index, false, 0,
                                                     "new.old");
                    auto* next = builder_.create_add(old,
                                                     module_->constant_int(size_type(), 1),
                                                     "new.next");
                    builder_.create_store(next, index);
                    emit_branch(condition_block);
                    builder_.set_insert_point(end_block);
                } else if (constructor) {
                    std::vector<ir::Value*> arguments{pointer};
                    for (std::size_t index = 0;
                         index < constructor_arguments.size(); ++index) {
                        if (!constructor_arguments[index]) {
                            continue;
                        }
                        auto argument =
                            emit_expression(*constructor_arguments[index]);
                        if (!argument.value) {
                            continue;
                        }
                        const auto parameter_index = index + 1;
                        if (parameter_index <
                            constructor->type->parameters.size()) {
                            argument.value = promote(
                                argument.value, argument.type,
                                constructor->type->parameters[parameter_index]);
                        }
                        arguments.push_back(argument.value);
                    }
                    create_call_instruction(constructor, std::move(arguments),
                                         nullptr, "new.ctor");
                } else if (!array && (has_defaults || has_member_ctors ||
                                      has_base_ctor)) {
                    emit_defaults(pointer);
                }
            }
        }
        return {pointer, pointer->type};
    }
    if (expression.text == "delete" || expression.text == "delete[]") {
        auto operand = emit_expression(*expression.children.front());
        if (!operand.value) {
            return {};
        }
        ir::BasicBlock* done_block = nullptr;
        if (operand.value) {
            auto* nonnull_block =
                current_function_->add_block("delete.nonnull");
            done_block = current_function_->add_block("delete.done");
            auto* null_value = module_->constant_null(operand.type);
            auto* is_null = builder_.create_icmp(
                ir::ICmpPredicate::Eq, operand.value, null_value, "delete.isnull");
            emit_conditional_branch(is_null, done_block, nonnull_block);
            builder_.set_insert_point(nonnull_block);
            const auto element = operand.type && operand.type->kind == ir::TypeKind::Pointer
                                     ? operand.type->element
                                     : nullptr;
            if (expression.text == "delete" && element &&
                element->kind == ir::TypeKind::Struct) {
                const auto record = ir_records_.find(element);
                if (record != ir_records_.end() && record->second) {
                    const auto destructor_name = "~" + record->second->tag;
                    const int slot =
                        virtual_slot(*record->second, destructor_name);
                    if (slot >= 0 &&
                        method_is_virtual(*record->second, destructor_name)) {
                        const auto methods =
                            class_methods_.find(record->second->tag);
                        ir::Function* destructor = nullptr;
                        if (methods != class_methods_.end()) {
                            const auto found =
                                methods->second.find(destructor_name);
                            if (found != methods->second.end()) {
                                destructor = found->second;
                            }
                        }
                        if (destructor) {
                            const auto byte_type = module_->types().int_type(8);
                            auto* zero = module_->constant_int(int_type(), 0);
                            auto* vptr_slot = builder_.create_gep(
                                element, operand.value, {zero, zero}, false,
                                "delete.vptr");
                            vptr_slot->type = module_->types().pointer_type(
                                module_->types().pointer_type(byte_type));
                            auto* table = builder_.create_load(
                                module_->types().pointer_type(byte_type), vptr_slot,
                                false, 0, "delete.vtable");
                            auto* function_slot = builder_.create_gep(
                                byte_type, table,
                                {module_->constant_int(
                                    size_type(),
                                    static_cast<std::int64_t>(slot) * static_cast<std::int64_t>(target::get_target(module_->target()).pointer_bits / 8))},
                                false, "delete.slot");
                            function_slot->type = module_->types().pointer_type(
                                module_->types().pointer_type(byte_type));
                            auto* function = builder_.create_load(
                                module_->types().pointer_type(byte_type),
                                function_slot, false, 0, "delete.vfn");
                            auto* callee = builder_.create(
                                ir::Opcode::BitCast,
                                module_->types().pointer_type(destructor->type),
                                {function}, "delete.dtor.ptr");
                            auto* self = operand.value;
                            if (self->type != destructor->type->parameters[0]) {
                                self = builder_.create(
                                    ir::Opcode::BitCast,
                                    destructor->type->parameters[0], {self},
                                    "delete.dtor.this");
                            }
                            create_call_instruction(callee, {self}, nullptr,
                                                 "delete.dtor");
                        }
                    }
                    const auto delete_methods =
                        class_methods_.find(record->second->tag);
                    const bool has_own_destructor =
                        delete_methods != class_methods_.end() &&
                        delete_methods->second.find(destructor_name) !=
                            delete_methods->second.end();
                    if (!has_own_destructor || slot < 0) {
                        auto* member_destructor =
                            ensure_member_destructor(record->second);
                        if (member_destructor) {
                            auto* self = operand.value;
                            if (self->type !=
                                member_destructor->type->parameters[0]) {
                                self = builder_.create(
                                    ir::Opcode::BitCast,
                                    member_destructor->type->parameters[0],
                                    {self}, "delete.member.this");
                            }
                            create_call_instruction(member_destructor, {self},
                                                 nullptr,
                                                 "delete.member.dtor");
                        }
                    }
                }
            }
        }
        auto* free_function = functions_.find("free") != functions_.end()
                                  ? functions_["free"]
                                  : nullptr;
        if (!free_function) {
            auto* function_type = module_->types().function_type(
                module_->types().void_type(),
                {module_->types().pointer_type(module_->types().void_type())}, false);
            free_function = module_->add_function("free", function_type,
                                                  ir::Linkage::External, true);
            free_function->add_parameter(
                module_->types().pointer_type(module_->types().void_type()), "pointer");
            functions_["free"] = free_function;
        }
        auto* cast = builder_.create(
            ir::Opcode::BitCast,
            module_->types().pointer_type(module_->types().void_type()), {operand.value},
            "delete.raw");
        if (expression.text == "delete[]" && operand.type &&
            operand.type->kind == ir::TypeKind::Pointer) {
            auto* raw = builder_.create(
                ir::Opcode::BitCast,
                module_->types().pointer_type(module_->types().int_type(8)),
                {operand.value}, "delete.bytes");
            auto* base = builder_.create_gep(
                module_->types().int_type(8), raw,
                {module_->constant_int(size_type(), -4)}, false, "delete.base");
            auto* count_pointer = builder_.create(
                ir::Opcode::BitCast, module_->types().pointer_type(size_type()), {base},
                "delete.count.ptr");
            auto* count = builder_.create_load(size_type(), count_pointer, false, 0,
                                               "delete.count");
            const auto element = operand.type->element;
            ir::Function* destructor = nullptr;
            int destructor_slot = -1;
            if (element && element->kind == ir::TypeKind::Struct) {
                const auto record = ir_records_.find(element);
                if (record != ir_records_.end()) {
                    if (record->second && record->second->polymorphic) {
                        const auto destructor_name =
                            "~" + record->second->tag;
                        if (method_is_virtual(*record->second,
                                              destructor_name)) {
                            destructor_slot = virtual_slot(
                                *record->second, destructor_name);
                        }
                    }
                    for (auto* current = record->second; current != nullptr;
                         current = current->base && current->base->record
                                       ? current->base->record.get()
                                       : nullptr) {
                        const auto methods = class_methods_.find(current->tag);
                        if (methods == class_methods_.end()) {
                            continue;
                        }
                        const auto found = methods->second.find("~" + current->tag);
                        if (found != methods->second.end()) {
                            destructor = found->second;
                            break;
                        }
                    }
                    if (destructor_slot < 0) {
                        destructor =
                            ensure_member_destructor(record->second);
                    }
                }
            }
            if (destructor) {
                auto* index = builder_.create_alloca(size_type(), 0, "delete.index");
                builder_.create_store(module_->constant_int(size_type(), 0), index);
                auto* condition_block = current_function_->add_block("delete.cond");
                auto* body_block = current_function_->add_block("delete.body");
                auto* increment_block = current_function_->add_block("delete.inc");
                auto* end_block = current_function_->add_block("delete.end");
                emit_branch(condition_block);
                builder_.set_insert_point(condition_block);
                auto* current =
                    builder_.create_load(size_type(), index, false, 0, "delete.i");
                auto* condition = builder_.create_icmp(ir::ICmpPredicate::Ult, current,
                                                       count, "delete.cmp");
                emit_conditional_branch(condition, body_block, end_block);
                builder_.set_insert_point(body_block);
                auto* element_pointer = builder_.create_gep(
                    operand.type->element, operand.value, {current}, false,
                    "delete.element");
                element_pointer->type =
                    module_->types().pointer_type(operand.type->element);
                auto* self = element_pointer;
                if (self->type != destructor->type->parameters[0]) {
                    self = builder_.create(ir::Opcode::BitCast,
                                           destructor->type->parameters[0], {self},
                                           "delete.this");
                }
                if (destructor_slot >= 0) {
                    const auto byte_type = module_->types().int_type(8);
                    auto* zero = module_->constant_int(int_type(), 0);
                    auto* vptr_slot = builder_.create_gep(
                        self->type->element, self, {zero, zero}, false,
                        "delete.vptr");
                    vptr_slot->type = module_->types().pointer_type(
                        module_->types().pointer_type(byte_type));
                    auto* table = builder_.create_load(
                        module_->types().pointer_type(byte_type), vptr_slot,
                        false, 0, "delete.vtable");
                    auto* function_slot = builder_.create_gep(
                        byte_type, table,
                        {module_->constant_int(
                            size_type(),
                            static_cast<std::int64_t>(destructor_slot) * static_cast<std::int64_t>(target::get_target(module_->target()).pointer_bits / 8))},
                        false, "delete.slot");
                    function_slot->type = module_->types().pointer_type(
                        module_->types().pointer_type(byte_type));
                    auto* function = builder_.create_load(
                        module_->types().pointer_type(byte_type), function_slot,
                        false, 0, "delete.vfn");
                    auto* callee = builder_.create(
                        ir::Opcode::BitCast,
                        module_->types().pointer_type(destructor->type),
                        {function}, "delete.vfn.ptr");
                    create_call_instruction(callee, {self}, nullptr,
                                         "delete.vdtor");
                } else {
                    create_call_instruction(destructor, {self}, nullptr,
                                         "delete.dtor");
                }
                emit_branch(increment_block);
                builder_.set_insert_point(increment_block);
                auto* old = builder_.create_load(size_type(), index, false, 0,
                                                 "delete.old");
                auto* next = builder_.create_add(old,
                                                 module_->constant_int(size_type(), 1),
                                                 "delete.next");
                builder_.create_store(next, index);
                emit_branch(condition_block);
                builder_.set_insert_point(end_block);
            }
            cast = builder_.create(
                ir::Opcode::BitCast,
                module_->types().pointer_type(module_->types().void_type()), {base},
                "delete.base.raw");
        }
        create_call_instruction(free_function, {cast}, nullptr, "delete.call");
        builder_.create_br(done_block);
        builder_.set_insert_point(done_block);
        return {nullptr, module_->types().void_type()};
    }
    if (expression.text == "-" || expression.text == "!" ||
        expression.text == "~" || expression.text == "++" ||
        expression.text == "--") {
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
                            methods->second.find("operator" + expression.text + "#0");
                        if (method == methods->second.end()) {
                            method =
                                methods->second.find("operator" + expression.text);
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
                                        {self_value}, "unary.this");
                                }
                                auto* call = create_call_instruction(
                                    method->second, {self_value},
                                    method->second->type->return_type,
                                    "unary.operator");
                                return reference_result(call, method->second, method->second->type->return_type);


                            }
                        }
                    }
                }
            }
        }
    }
    if ((expression.text == "-" || expression.text == "~") &&
        !expression.children.empty() &&
        expression.children.front() &&
        expression.children.front()->kind ==
            frontend::AstKind::CallExpression) {
        auto record_value = emit_expression(*expression.children.front());
        if (record_value.value && record_value.type &&
            record_value.type->kind == ir::TypeKind::Struct) {
            auto* storage = builder_.create_alloca(record_value.type, 0,
                                                       "unary.temp");
            builder_.create_store(record_value.value, storage);
            const auto record = ir_records_.find(record_value.type);
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
                                {self}, "unary.this");
                        }
                        auto* call = create_call_instruction(
                            method->second, {self},
                            method->second->type->return_type,
                            "unary.conversion");
                        if (expression.text == "-") {
                            auto* zero = module_->constant_int(
                                method->second->type->return_type, 0);
                            auto* value = builder_.create_sub(
                                zero, call, "negate");
                            return {value, value->type};
                        }
                        auto* mask = module_->constant_int(
                            method->second->type->return_type, -1);
                        auto* value = builder_.create(
                            ir::Opcode::Xor, call->type, {call, mask}, "bitnot");
                        return {value, value->type};
                    }
                }
            }
        }
    }
    // A !call() emits the call once and keeps the value: the generic path
    // below would emit it again, running its side effects twice.
    std::optional<Expression> unary_operand;
    if (expression.text == "!" && !expression.children.empty() &&
        expression.children.front() &&
        expression.children.front()->kind ==
            frontend::AstKind::CallExpression) {
        auto record_value = emit_expression(*expression.children.front());
        unary_operand = record_value;
        if (record_value.value && record_value.type &&
            record_value.type->kind == ir::TypeKind::Struct) {
            auto* storage = builder_.create_alloca(record_value.type, 0,
                                                       "not.temp");
            builder_.create_store(record_value.value, storage);
            const auto record = ir_records_.find(record_value.type);
            if (record != ir_records_.end() && record->second) {
                const auto methods =
                    class_methods_.find(record->second->tag);
                if (methods != class_methods_.end()) {
                    auto method = methods->second.find("operator bool");
                    if (method == methods->second.end()) {
                        method = methods->second.find("operator int");
                    }
                    if (method != methods->second.end()) {
                        auto* self = storage;
                        if (self->type !=
                            method->second->type->parameters[0]) {
                            self = builder_.create(
                                ir::Opcode::BitCast,
                                method->second->type->parameters[0],
                                {self}, "not.this");
                        }
                        auto* call = create_call_instruction(
                            method->second, {self},
                            method->second->type->return_type,
                            "not.bool");
                        auto* zero = module_->constant_int(
                            method->second->type->return_type, 0);
                        auto* value = builder_.create_icmp(
                            ir::ICmpPredicate::Eq, call, zero, "not");
                        return {value, value->type};
                    }
                }
            }
        }
    }
    if (expression.text == "!" && !expression.children.empty() &&
        expression.children.front() &&
        expression.children.front()->kind == frontend::AstKind::Identifier) {
        if (auto* local = lookup_local(expression.children.front()->text)) {
            // A reference to a record names the record, so s[i] on a
            // reference parameter reaches operator[] like a direct object.
            const auto object_type = record_object_type(local->frontend_type);
            if (object_type && object_type->kind == frontend::TypeKind::Record &&
                object_type->record) {
                const auto methods = class_methods_.find(object_type->record->tag);
                if (methods != class_methods_.end()) {
                    auto method = methods->second.find("operator bool");
                    if (method == methods->second.end()) {
                        method = methods->second.find("operator int");
                    }
                    if (method != methods->second.end()) {
                        auto self = emit_lvalue(*expression.children.front());
                        if (self.value) {
                            if (self.value->type !=
                                method->second->type->parameters[0]) {
                                self.value = builder_.create(
                                    ir::Opcode::BitCast,
                                    method->second->type->parameters[0],
                                    {self.value}, "not.this");
                            }
                            auto* call = create_call_instruction(
                                method->second, {self.value},
                                method->second->type->return_type,
                                "not.bool");
                            auto* zero = module_->constant_int(
                                method->second->type->return_type, 0);
                            auto* value = builder_.create_icmp(
                                ir::ICmpPredicate::Eq, call, zero, "not");
                            return {value, value->type};
                        }
                    }
                }
            }
        }
    }
    auto operand = unary_operand ? *unary_operand
                                  : emit_expression(*expression.children.front());
    if (!operand.value) {
        return {};
    }
    if (expression.text == "+" ) {
        return operand;
    }
    if (expression.text == "-") {
        if (ir::is_float(operand.type)) {
            auto* zero = module_->constant_float(operand.type, 0.0);
            return {builder_.create(ir::Opcode::FSub, operand.type, {zero, operand.value},
                                    "fneg"),
                    operand.type};
        }
        auto* zero = module_->constant_int(operand.type, 0);
        return {builder_.create_sub(zero, operand.value, "neg"), operand.type};
    }
    if (expression.text == "~") {
        auto* minus_one = module_->constant_int(operand.type, -1);
        return {builder_.create_xor(operand.value, minus_one, "not"), operand.type};
    }
    if (expression.text == "!") {
        // `!` applies to pointers as well as integers. An integer constant
        // cannot carry a pointer type and the comparison wants matching
        // operands, so a pointer is reduced to an integer first; an integer
        // operand keeps its own width.
        auto* compared = operand.value;
        ir::TypePtr compare_type = operand.type;
        if (operand.type && operand.type->kind == ir::TypeKind::Pointer) {
            compared = builder_.create(ir::Opcode::PtrToInt, int_type(),
                                       {operand.value}, "not.int");
            compare_type = int_type();
        }
        auto* zero = module_->constant_int(compare_type, 0);
        auto* value = builder_.create_icmp(ir::ICmpPredicate::Eq, compared, zero, "not");
        return {value, value->type};
    }
    if (expression.text == "++" || expression.text == "--") {
        auto address = emit_lvalue(*expression.children.front());
        if (!address.value) {
            return {};
        }
        auto* old = builder_.create_load(address.type, address.value, false, 0, "inc.old");
        ir::Value* updated = nullptr;
        if (address.type && address.type->kind == ir::TypeKind::Pointer) {
            auto* one = module_->constant_int(int_type(), expression.text == "++" ? 1 : -1);
            updated = builder_.create_gep(address.type->element, old, {one}, false,
                                          "inc.new");
            updated->type = address.type;
        } else {
            auto* one = module_->constant_int(address.type, 1);
            updated = expression.text == "++"
                          ? builder_.create_add(old, one, "inc.new")
                          : builder_.create_sub(old, one, "inc.new");
        }
        builder_.create_store(updated, address.value);
        return {updated, address.type};
    }
    return operand;
}

CIRGen::Expression CIRGen::emit_lvalue(const frontend::AstNode& expression) {
    if (expression.kind == frontend::AstKind::CompoundLiteral) {
        return emit_expression(expression);
    }
    if (expression.kind == frontend::AstKind::TypeIdExpression) {
        return emit_typeid_reference(expression);
    }
    if (expression.kind == frontend::AstKind::CastExpression &&
        !expression.children.empty() && expression.children.front()) {
        const auto target =
            sema_.type_from_type_name(*expression.children.front());
        if (target && target->is_reference && target->element) {
            // A static cast to a reference names its operand, so the address is
            // the operand's: the cast itself yields a value for a non-record
            // reference. A dynamic cast is different -- it checks the object at
            // run time and answers with its own address.
            const bool dynamic = !expression.labels.empty() &&
                                 expression.labels.front() == "dynamic_cast";
            if (!dynamic) {
                auto address = emit_lvalue(*expression.children[1]);
                if (address.value) {
                    return {address.value, map_type(target->element)};
                }
                return {};
            }
            auto cast = emit_cast(expression);
            if (cast.value) {
                return {cast.value, map_type(target->element)};
            }
            return {};
        }
    }
    if (expression.kind == frontend::AstKind::Identifier) {
        if (auto* local = lookup_local(expression.text)) {
            if (local->frontend_type && local->frontend_type->is_reference &&
                local->type && local->type->kind == ir::TypeKind::Pointer) {
                auto* pointer = builder_.create_load(local->type, local->address, false, 0,
                                                     expression.text + ".ref");
                return {pointer, local->type->element};
            }
            Expression result;
            result.value = local->address;
            result.type = local->type;
            result.is_volatile =
                local->frontend_type && local->frontend_type->is_volatile;
            return result;
        }
        const auto global = globals_.find(expression.text);
        if (global != globals_.end()) {
            Expression result;
            result.value = global->second;
            result.type = global->second->element_type;
            result.is_volatile = volatile_globals_.contains(global->second);
            return result;
        }
        if (current_class_) {
            const auto static_global =
                globals_.find(current_class_->tag + "::" + expression.text);
            if (static_global != globals_.end()) {
                Expression result;
                result.value = static_global->second;
                result.type = static_global->second->element_type;
                result.is_volatile =
                    volatile_globals_.contains(static_global->second);
                return result;
            }
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
                return emit_implicit_member_lvalue(expression.text);
            }
        }
        error(expression.location, "identifier '" + expression.text + "' has no address");
        return {};
    }
    if (expression.kind == frontend::AstKind::UnaryExpression && expression.text == "*" &&
        !expression.children.empty() && expression.children.front()) {
        auto pointer = emit_expression(*expression.children.front());
        if (!pointer.value || !pointer.type ||
            pointer.type->kind != ir::TypeKind::Pointer) {
            // (*owner).member writes the object the overloaded dereference
            // refers to.
            if (auto* overload = dereference_overload(pointer)) {
                auto object = emit_lvalue(*expression.children.front());
                return unary_operator_call(overload, object, true);
            }
            error(expression.location, "dereference of a non-pointer lvalue");
            return {};
        }
        Expression reference;
        reference.value = pointer.value;
        reference.type = pointer.type->element;
        // `*p = value` through a volatile-qualified pointer is a volatile store.
        reference.is_volatile =
            dereference_is_volatile(*expression.children.front());
        return reference;
    }
    if (expression.kind == frontend::AstKind::SubscriptExpression) {
        if (expression.children.size() < 2) {
            return {};
        }
        // (*this)[i] = v writes the object the operand refers to, so the
        // record's operator[] receives that address.
        if (expression.children[0] &&
            expression.children[0]->kind == frontend::AstKind::UnaryExpression &&
            expression.children[0]->text == "*") {
            auto self = emit_lvalue(*expression.children[0]);
            if (self.value && self.type) {
                const auto record = ir_records_.find(self.type);
                if (record != ir_records_.end() && record->second) {
                    const auto methods = class_methods_.find(record->second->tag);
                    if (methods != class_methods_.end()) {
                        const auto method = methods->second.find("operator[]");
                        if (method != methods->second.end()) {
                            return subscript_operator(method->second, self,
                                                     *expression.children[1], true);
                        }
                    }
                }
            }
        }
        if (expression.children[0] &&
            expression.children[0]->kind == frontend::AstKind::Identifier) {
            if (auto* local = lookup_local(expression.children[0]->text)) {
                const auto object_type =
                    record_object_type(local->frontend_type);
                if (object_type &&
                    object_type->kind == frontend::TypeKind::Record &&
                    object_type->record) {
                    const auto methods =
                        class_methods_.find(object_type->record->tag);
                    if (methods != class_methods_.end()) {
                        const auto method = methods->second.find("operator[]");
                        if (method != methods->second.end()) {
                            auto self = emit_lvalue(*expression.children[0]);
                            auto index = emit_expression(*expression.children[1]);
                            if (self.value && index.value) {
                                auto* function = method->second;
                                if (self.value->type !=
                                    function->type->parameters[0]) {
                                    self.value = builder_.create(
                                        ir::Opcode::BitCast,
                                        function->type->parameters[0],
                                        {self.value}, "subscript.this");
                                }
                                std::vector<ir::Value*> arguments{self.value};
                                if (index.value->type !=
                                    function->type->parameters[1]) {
                                    index.value = promote(
                                        index.value, index.type,
                                        function->type->parameters[1]);
                                }
                                arguments.push_back(index.value);
                                auto* call = create_call_instruction(
                                    function, std::move(arguments),
                                    function->type->return_type,
                                    "subscript.lvalue");
                                if (function->type->return_type &&
                                    function->type->return_type->kind ==
                                        ir::TypeKind::Pointer) {
                                    return {call,
                                            function->type->return_type->element};
                                }
                                error(expression.location,
                                      "operator[] does not return a reference");
                                return {};
                            }
                        }
                    }
                }
            }
        }
        auto base = emit_expression(*expression.children[0]);
        auto index = emit_expression(*expression.children[1]);
        if (!base.value || !base.type || base.type->kind != ir::TypeKind::Pointer) {
            error(expression.location, "subscripted lvalue is not a pointer");
            return {};
        }
        auto* address = builder_.create_gep(base.type->element, base.value, {index.value},
                                            false, "element");
        address->type = base.type;
        return {address, base.type->element};
    }
    if (expression.kind == frontend::AstKind::MemberExpression) {
        return emit_member_lvalue(expression);
    }
    if (is_move_call(expression)) {
        // A move names its argument, so its lvalue is the argument's.
        return emit_lvalue(*expression.children[1]->children.front());
    }
    if (expression.kind == frontend::AstKind::CallExpression) {
        // A reference-returning call names the object it refers to; any other
        // call has no address of its own, so callers that need one for the
        // value it returns get a temporary. `reference_result` records the
        // address it answered with, which is the lvalue for `front() = 11`.
        last_reference_address_ = nullptr;
        auto value = emit_expression(expression);
        if (last_reference_address_ != nullptr) {
            auto* address = last_reference_address_;
            last_reference_address_ = nullptr;
            return {address, address->type && address->type->element
                                 ? address->type->element
                                 : value.type};
        }
        if (value.value && value.type) {
            auto* storage = builder_.create_alloca(value.type, 0, "call.temp");
            builder_.create_store(value.value, storage);
            return {storage, value.type};
        }
        return {};
    }
    // An overloaded operator is a binary or unary expression rather than a
    // CallExpression, but it returns a value exactly the way a call does. A
    // record result is materialised into a temporary, which is the lvalue a
    // copy-initialisation or an assignment from `a + b` needs; a scalar result
    // is not an lvalue and keeps the diagnostic below.
    if ((expression.kind == frontend::AstKind::BinaryExpression ||
         expression.kind == frontend::AstKind::UnaryExpression)) {
        last_reference_address_ = nullptr;
        auto value = emit_expression(expression);
        if (value.value && value.type &&
            (value.type->kind == ir::TypeKind::Struct ||
             value.type->kind == ir::TypeKind::Union)) {
            auto* storage = builder_.create_alloca(value.type, 0, "operator.temp");
            builder_.create_store(value.value, storage);
            return {storage, value.type};
        }
        return {};
    }
    error(expression.location, "expression is not an lvalue");
    return {};
}

CIRGen::Expression CIRGen::emit_assignment(const frontend::AstNode& expression) {
    if (expression.children.size() < 2 || !expression.children[0] ||
        !expression.children[1]) {
        return {};
    }
    auto address = emit_lvalue(*expression.children[0]);
    if (!address.value) {
        return {};
    }
    if (address.type && address.type->kind == ir::TypeKind::Struct &&
        expression.text != "=") {
        const auto record = ir_records_.find(address.type);
        if (record != ir_records_.end()) {
            const auto methods = class_methods_.find(record->second->tag);
            if (methods != class_methods_.end()) {
                const auto method =
                    methods->second.find("operator" + expression.text);
                if (method != methods->second.end()) {
                    auto source = emit_lvalue(*expression.children[1]);
                    if (!source.value) {
                        return {};
                    }
                    auto* self = address.value;
                    if (self->type != method->second->type->parameters[0]) {
                        self = builder_.create(
                            ir::Opcode::BitCast, method->second->type->parameters[0],
                            {self}, "assign.this");
                    }
                    create_call_instruction(method->second, {self, source.value}, nullptr,
                                         "assign.compound");
                    return {nullptr, module_->types().void_type()};
                }
            }
        }
    }
    if (address.type && address.type->kind == ir::TypeKind::Struct &&
        expression.text == "=") {
        const auto record = ir_records_.find(address.type);
        if (record != ir_records_.end()) {
            int reference_mode = 1;
            if (is_move_call(*expression.children[1])) {
                reference_mode = 2;
            } else if (expression.children[1]->kind ==
                           frontend::AstKind::CastExpression &&
                       expression.children[1]->children.size() > 1 &&
                       expression.children[1]->children.front()) {
                const auto cast_type = sema_.type_from_type_name(
                    *expression.children[1]->children.front());
                if (cast_type && cast_type->is_rvalue_reference) {
                    reference_mode = 2;
                }
            }
            auto* method = select_assignment(record->second->tag, reference_mode);
            if (method) {
                const frontend::AstNode* source_node = expression.children[1].get();
                if (is_move_call(*source_node)) {
                    const auto& arguments = source_node->children[1]->children;
                    if (!arguments.empty() && arguments.front()) {
                        source_node = arguments.front().get();
                    }
                } else if (source_node->kind == frontend::AstKind::CastExpression &&
                           source_node->children.size() > 1) {
                    source_node = source_node->children[1].get();
                }
                Expression source;
                if (source_node->kind ==
                    frontend::AstKind::AssignmentExpression) {
                    source = emit_expression(*source_node);
                } else {
                    source = emit_lvalue(*source_node);
                }
                if (source.value) {
                    auto* self = address.value;
                    if (self->type != method->type->parameters[0]) {
                        self = builder_.create(
                            ir::Opcode::BitCast, method->type->parameters[0], {self},
                            "assign.this");
                    }
                    auto* call = create_call_instruction(
                        method, {self, source.value}, method->type->return_type,
                        "assign.operator");
                    if (method->type->return_type &&
                        !ir::is_void(method->type->return_type)) {
                        return reference_result(call, method, method->type->return_type);
                    }
                    return {nullptr, module_->types().void_type()};
                }
            }
        }
    }
    if (address.type && address.type->kind == ir::TypeKind::Struct &&
        expression.text == "=") {
        auto source = emit_lvalue(*expression.children[1]);
        if (source.value && source.type && source.type->kind == ir::TypeKind::Struct) {
            emit_copy(address.value, source.value, address.type);
            return {address.value, address.type};
        }
    }
    auto right = emit_expression(*expression.children[1]);
    if (!right.value) {
        return {};
    }
    if (address.type && address.type->kind == ir::TypeKind::Struct && expression.text == "=" &&
        right.type && right.type->kind == ir::TypeKind::Struct) {
        emit_copy(address.value, right.value, address.type);
        return {address.value, address.type};
    }
    if (address.bit_field) {
        auto* converted = promote(right.value, right.type, address.type);
        auto* old = builder_.create_load(address.type, address.value,
                                         address.is_volatile, 0,
                                         "bitfield.old");
        std::uint64_t mask = address.bit_width >= 64
                                 ? ~0ull
                                 : ((1ull << address.bit_width) - 1);
        auto* mask_value = module_->constant_int(address.type, mask);
        auto* inverse =
            module_->constant_int(address.type, ~(mask << address.bit_offset));
        auto* cleared = builder_.create_and(old, inverse, "bitfield.clear");
        auto* shift = module_->constant_int(address.type, address.bit_offset);
        auto* narrowed = builder_.create_and(converted, mask_value, "bitfield.value");
        auto* shifted = builder_.create_shl(narrowed, shift, "bitfield.shift");
        auto* inserted = builder_.create_and(shifted, [&] {
            auto* wide = module_->constant_int(address.type, mask << address.bit_offset);
            return wide;
        }(), "bitfield.insert");
        auto* combined = builder_.create_or(cleared, inserted, "bitfield.combine");
        builder_.create_store(combined, address.value,
                              address.is_volatile, 0);
        return {converted, address.type};
    }
    if (address.type && address.type->kind == ir::TypeKind::Pointer &&
        (expression.text == "+=" || expression.text == "-=")) {
        auto* left = builder_.create_load(address.type, address.value, false, 0, "assign.old");
        auto* index = promote(right.value, right.type, int_type());
        if (expression.text == "-=") {
            auto* zero = module_->constant_int(int_type(), 0);
            index = builder_.create_sub(zero, index, "neg");
        }
        auto* updated = builder_.create_gep(address.type->element, left, {index}, false,
                                            "ptradd");
        updated->type = address.type;
        builder_.create_store(updated, address.value);
        return {updated, address.type};
    }
    if (address.type && (ir::is_integer(address.type) ||
                          ir::is_float(address.type)) &&
        right.value && right.type &&
        right.type->kind == ir::TypeKind::Struct &&
        expression.children[1]->kind ==
            frontend::AstKind::CallExpression) {
        auto* storage = builder_.create_alloca(right.type, 0,
                                                   "assign.temp");
        builder_.create_store(right.value, storage);
        const auto record = ir_records_.find(right.type);
        if (record != ir_records_.end() && record->second) {
            const auto methods = class_methods_.find(record->second->tag);
            if (methods != class_methods_.end()) {
                const char* conversion = nullptr;
                if (ir::is_integer(address.type)) {
                    conversion = address.type->bits == 1 ? "operator bool"
                                                         : "operator int";
                } else {
                    conversion = address.type->bits == 64
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
                            method->second->type->parameters[0], {self},
                            "assign.this");
                    }
                    auto* call = create_call_instruction(
                        method->second, {self},
                        method->second->type->return_type,
                        "assign.conversion");
                    right.value = call;
                    right.type = method->second->type->return_type;
                }
            }
        }
    }
    auto* value = promote(right.value, right.type, address.type);
    if (expression.text != "=") {
        auto* left = builder_.create_load(address.type, address.value, false, 0, "assign.old");
        ir::Opcode opcode = ir::Opcode::Add;
        if (expression.text == "-=") {
            opcode = ir::Opcode::Sub;
        } else if (expression.text == "*=") {
            opcode = ir::Opcode::Mul;
        } else if (expression.text == "/=") {
            opcode = ir::Opcode::SDiv;
        } else if (expression.text == "%=") {
            opcode = ir::Opcode::SRem;
        } else if (expression.text == "<<=") {
            opcode = ir::Opcode::Shl;
        } else if (expression.text == ">>=") {
            opcode = ir::Opcode::AShr;
        } else if (expression.text == "&=") {
            opcode = ir::Opcode::And;
        } else if (expression.text == "|=") {
            opcode = ir::Opcode::Or;
        } else if (expression.text == "^=") {
            opcode = ir::Opcode::Xor;
        }
        if (ir::is_float(address.type)) {
            if (expression.text == "+=") {
                opcode = ir::Opcode::FAdd;
            } else if (expression.text == "-=") {
                opcode = ir::Opcode::FSub;
            } else if (expression.text == "*=") {
                opcode = ir::Opcode::FMul;
            } else if (expression.text == "/=") {
                opcode = ir::Opcode::FDiv;
            }
        }
        value = builder_.create(opcode, address.type, {left, value}, "assign.op");
    }
    builder_.create_store(value, address.value, address.is_volatile);
    return {value, address.type};
}

CIRGen::Expression CIRGen::emit_matrix_intrinsic(const frontend::AstNode& expression,
                                                 const ir::Function& callee,
                                                 ir::Intrinsic intrinsic) {
    if (!profile_.has_matrix) {
        error(expression.location, "matrix intrinsics are not supported for target '" +
                                       std::string(profile_.name) + "'");
        return {};
    }
    if (expression.children.size() < 2 || !expression.children[1]) {
        return {};
    }
    std::vector<ir::Value*> arguments;
    for (const auto& node : expression.children[1]->children) {
        if (!node) {
            continue;
        }
        auto argument = emit_lvalue(*node);
        if (!argument.value) {
            error(node->location, "matrix intrinsic argument is not an lvalue");
            return {};
        }
        arguments.push_back(argument.value);
    }
    const auto result_type = callee.type ? callee.type->return_type : nullptr;
    auto* call = builder_.create_intrinsic(intrinsic, result_type, std::move(arguments),
                                           "intrinsic");
    return {call, result_type};
}

CIRGen::Expression CIRGen::reference_result(ir::Value* call, ir::Function* callee,
                                            ir::TypePtr result_type) {
    if (!call || !callee || !result_type ||
        result_type->kind != ir::TypeKind::Pointer) {
        return {call, result_type};
    }
    const auto ast_type = function_ast_types_.find(callee);
    if (ast_type == function_ast_types_.end() || !ast_type->second ||
        !ast_type->second->return_type ||
        !ast_type->second->return_type->is_reference) {
        return {call, result_type};
    }
    // A reference-returning call answers with the address of the object (ADR
    // 0036). The address is kept aside as well as returned: a value context
    // wants the loaded value, and the assignment path needs the address, so
    // `values.front() = 11` can write the element instead of a temporary.
    last_reference_address_ = call;
    // A record value is its address in this IR, so a record keeps it; only a
    // scalar or floating element is read, because a value context wants the
    // value there.
    if (result_type->element &&
        (result_type->element->kind == ir::TypeKind::Struct ||
         result_type->element->kind == ir::TypeKind::Array)) {
        return {call, result_type};
    }
    return {builder_.create_load(result_type->element, call, false, 0,
                                 "reference.result"),
            result_type->element};
}
CIRGen::Expression CIRGen::emit_call(const frontend::AstNode& expression) {
    if (expression.children.size() < 2 || !expression.children[0] ||
        !expression.children[1]) {
        return {};
    }
    if (is_move_call(expression)) {
        // `std::move(x)` and `std::forward<T>(x)` name x and are typed as an
        // rvalue reference, so in a value context a scalar move is the
        // referent's value. A record keeps its address -- a record value is its
        // address here -- and the ordinary call path already answers correctly
        // for that, so this branch only settles the scalar case.
        auto address = emit_lvalue(*expression.children[1]->children.front());
        if (address.value) {
            auto* element = address.value->type &&
                                    address.value->type->kind == ir::TypeKind::Pointer
                                ? address.value->type->element
                                : nullptr;
            if (element && (element->kind == ir::TypeKind::Struct ||
                            element->kind == ir::TypeKind::Array)) {
                // A record value is its address, and it must carry the record
                // type: an initialiser compares the emitted type against its
                // target's, and a pointer type never matches, which sent a moved
                // record down the aggregate path.
                return {address.value, element};
            }
            if (element) {
                return {builder_.create_load(element, address.value, false, 0,
                                             "move.value"),
                        element};
            }
        }
    }
    if (expression.children[0]->kind == frontend::AstKind::Identifier &&
        (expression.children[0]->text == "std::min" ||
         expression.children[0]->text == "std::max")) {
        const auto& arguments = expression.children[1]->children;
        if (arguments.size() < 2 || !arguments[0] || !arguments[1]) {
            return {};
        }
        auto left = emit_expression(*arguments[0]);
        auto right = emit_expression(*arguments[1]);
        if (!left.value || !right.value || !left.type) {
            return {};
        }
        const bool minimum =
            expression.children[0]->text == "std::min";
        ir::Value* condition = nullptr;
        if (ir::is_integer(left.type)) {
            condition = builder_.create_icmp(
                minimum ? ir::ICmpPredicate::Slt
                        : ir::ICmpPredicate::Sgt,
                left.value, right.value, "minmax.compare");
        } else if (ir::is_float(left.type)) {
            condition = builder_.create_fcmp(
                minimum ? ir::FCmpPredicate::Olt
                        : ir::FCmpPredicate::Ogt,
                left.value, right.value, "minmax.compare");
        } else {
            return {};
        }
        auto* storage = builder_.create_alloca(left.type, 0, "minmax.temp");
        auto* then_block = current_function_->add_block("minmax.then");
        auto* else_block = current_function_->add_block("minmax.else");
        auto* end_block = current_function_->add_block("minmax.end");
        emit_conditional_branch(condition, then_block, else_block);
        builder_.set_insert_point(then_block);
        builder_.create_store(left.value, storage);
        builder_.create_br(end_block);
        builder_.set_insert_point(else_block);
        builder_.create_store(right.value, storage);
        builder_.create_br(end_block);
        builder_.set_insert_point(end_block);
        auto* loaded =
            builder_.create_load(left.type, storage, false, 0, "minmax.value");
        return {loaded, left.type};
    }
    if (expression.children[0]->kind == frontend::AstKind::Identifier &&
        expression.children[0]->text == "std::swap") {
        const auto& arguments = expression.children[1]->children;
        if (arguments.size() < 2 || !arguments[0] || !arguments[1]) {
            return {};
        }
        auto left = emit_lvalue(*arguments[0]);
        auto right = emit_lvalue(*arguments[1]);
        if (!left.value || !right.value || !left.type || !right.type) {
            return {};
        }
        auto* left_value =
            builder_.create_load(left.type, left.value, false, 0, "swap.left");
        auto* right_value =
            builder_.create_load(right.type, right.value, false, 0, "swap.right");
        builder_.create_store(right_value, left.value);
        builder_.create_store(left_value, right.value);
        return {nullptr, module_->types().void_type()};
    }
    if (expression.children[0]->kind == frontend::AstKind::Identifier &&
        expression.children[0]->text == "__krc_destroy") {
        const auto& arguments = expression.children[1]->children;
        if (arguments.size() != 1 || !arguments.front()) {
            return {};
        }
        auto argument = emit_expression(*arguments.front());
        if (!argument.value || !argument.type ||
            argument.type->kind != ir::TypeKind::Pointer ||
            !argument.type->element ||
            argument.type->element->kind != ir::TypeKind::Struct) {
            return {};
        }
        const auto record = ir_records_.find(argument.type->element);
        if (record != ir_records_.end() && record->second) {
            if (auto* cleanup = ensure_member_destructor(record->second)) {
                create_call_instruction(cleanup, {argument.value}, nullptr,
                                        "destroy");
            }
        }
        return {nullptr, module_->types().void_type()};
    }
    if (expression.children[0]->kind == frontend::AstKind::Identifier &&
        (expression.children[0]->text == "std::move" ||
         expression.children[0]->text == "std::forward")) {
        const auto& arguments = expression.children[1]->children;
        if (arguments.empty() || !arguments.front()) {
            return {};
        }
        auto argument = emit_lvalue(*arguments.front());
        if (!argument.value) {
            return {};
        }
        return {argument.value, module_->types().pointer_type(argument.type)};
    }
    if (expression.children[0]->kind == frontend::AstKind::Identifier &&
        !lookup_local(expression.children[0]->text) &&
        !functions_.contains(expression.children[0]->text)) {
        const auto cached = sema_.cached_expression_type(expression);
        const auto named = cached && cached->kind == frontend::TypeKind::Record
                               ? cached
                               : sema_.type_of_name(*expression.children[0]);
        if (named && named->kind == frontend::TypeKind::Record) {
            const auto type = map_type(named);
            const auto record = ir_records_.find(type);
            if (record != ir_records_.end() && record->second) {
                const auto& argument_nodes = expression.children[1]->children;
                auto* constructor = select_constructor(
                    record->second->tag, argument_nodes.size(), 0);
                // A constructor that exists only as a class template pattern's declaration
                // has no body to call (ADR 0032). A record with no members has nothing to
                // construct, so it falls through to the zero-initialising path below.
                // Emitting the call left `std::tuple<> t = std::tuple<>();` bound to an
                // undefined `tuple_ctor_0_value`; the local, the placement-new and this
                // temporary path each reach that constructor by a different route.
                if (constructor && constructor->declaration &&
                    record->second->members.empty()) {
                    constructor = nullptr;
                }
                auto* storage =
                    builder_.create_alloca(type, 0, "temporary.record");
                if (!constructor) {
                    // A class with no declared constructor is still constructible: an
                    // implicit default constructor zero-initialises it, exactly as a
                    // declared local of this type is initialised. Requiring a declared
                    // constructor made `One().value()` an error for a class with
                    // nothing wrong with it. A call passing arguments that match no
                    // constructor stays an error.
                    if (!argument_nodes.empty()) {
                        error(expression.location,
                              "constructor-initialized class has no constructor");
                        return {};
                    }
                    emit_zero_initializer(storage, type);
                    auto* initialized = builder_.create_load(
                        type, storage, false, 0, "temporary.value");
                    return {initialized, type};
                }
                std::vector<ir::Value*> arguments{storage};
                for (std::size_t index = 0; index < argument_nodes.size();
                     ++index) {
                    if (!argument_nodes[index]) {
                        continue;
                    }
                    auto argument = emit_expression(*argument_nodes[index]);
                    if (!argument.value) {
                        return {};
                    }
                    if (index + 1 < constructor->type->parameters.size()) {
                        argument.value = promote(
                            argument.value, argument.type,
                            constructor->type->parameters[index + 1]);
                    }
                    arguments.push_back(argument.value);
                }
                create_call_instruction(constructor, std::move(arguments), nullptr,
                                     "temporary.constructor");
                auto* loaded = builder_.create_load(
                    type, storage, false, 0, "temporary.value");
                return {loaded, type};
            }
        }
    }
    if (expression.children[0]->kind == frontend::AstKind::Identifier) {
        const auto lambda = lambda_locals_.find(expression.children[0]->text);
        if (lambda != lambda_locals_.end()) {
            auto* local = lookup_local(expression.children[0]->text);
            if (!local || !local->type) {
                return {};
            }
            const auto byte_type = module_->types().int_type(8);
            auto* closure = builder_.create_load(
                local->type, local->address, false, 0, "lambda.closure.load");
            auto* zero = module_->constant_int(int_type(), 0);
            auto* fn_slot = builder_.create_gep(
                lambda->second.closure, closure, {zero, zero}, false,
                "lambda.fn.slot");
            fn_slot->type = module_->types().pointer_type(
                module_->types().pointer_type(byte_type));
            auto* fn = builder_.create_load(
                module_->types().pointer_type(byte_type), fn_slot, false, 0,
                "lambda.fn");
            auto* callee = builder_.create(
                ir::Opcode::BitCast,
                module_->types().pointer_type(lambda->second.function), {fn},
                "lambda.fn.call");
            std::vector<ir::Value*> arguments{closure};
            const auto& argument_nodes = expression.children[1]->children;
            for (std::size_t index = 0; index < argument_nodes.size(); ++index) {
                if (!argument_nodes[index]) {
                    continue;
                }
                auto argument = emit_expression(*argument_nodes[index]);
                if (!argument.value) {
                    return {};
                }
                const auto parameter_index = index + 1;
                if (parameter_index <
                    lambda->second.function->parameters.size()) {
                    argument.value = promote(
                        argument.value, argument.type,
                        lambda->second.function->parameters[parameter_index]);
                }
                arguments.push_back(argument.value);
            }
            auto* call = create_call_instruction(
                callee, std::move(arguments),
                lambda->second.function->return_type, "lambda.call");
            // A lambda returns a value here, so the result is not dereferenced.
            return {call, lambda->second.function->return_type};
        }
    }
    if (expression.children[0]->kind == frontend::AstKind::Identifier) {
        if (auto* local = lookup_local(expression.children[0]->text)) {
            // A reference to a record names the record, so s[i] on a
            // reference parameter reaches operator[] like a direct object.
            const auto object_type = record_object_type(local->frontend_type);
            if (object_type && object_type->kind == frontend::TypeKind::Record &&
                object_type->record) {
                const auto methods = class_methods_.find(object_type->record->tag);
                if (methods != class_methods_.end()) {
                    const auto method = methods->second.find("operator()");
                    if (method != methods->second.end()) {
                        auto self = emit_lvalue(*expression.children[0]);
                        if (!self.value) {
                            return {};
                        }
                        if (self.value->type !=
                            method->second->type->parameters[0]) {
                            self.value = builder_.create(
                                ir::Opcode::BitCast,
                                method->second->type->parameters[0],
                                {self.value}, "call.this");
                        }
                        std::vector<ir::Value*> arguments{self.value};
                        const auto& argument_nodes = expression.children[1]->children;
                        for (std::size_t index = 0; index < argument_nodes.size();
                             ++index) {
                            if (!argument_nodes[index]) {
                                continue;
                            }
                            auto argument = emit_expression(*argument_nodes[index]);
                            if (!argument.value) {
                                return {};
                            }
                            const auto parameter_index = index + 1;
                            const ir::TypePtr parameter_type =
                                parameter_index <
                                        method->second->type->parameters.size()
                                    ? method->second->type->parameters[parameter_index]
                                    : nullptr;
                            if (parameter_type &&
                                parameter_type->kind == ir::TypeKind::Pointer &&
                                argument.value->type &&
                                argument.value->type->kind != ir::TypeKind::Pointer) {
                                // A reference parameter is a pointer, so the caller
                                // passes the object's address rather than a load of
                                // it; promoting the loaded value to a pointer is
                                // what made a const T& operator() receive zero.
                                // A literal bound to a const reference needs storage,
                                // so it is materialised rather than rejected.
                                auto* stored = static_cast<ir::Value*>(nullptr);
                                const auto& argument_node =
                                    *argument_nodes[index];
                                const bool argument_is_lvalue =
                                    argument_node.kind ==
                                        frontend::AstKind::Identifier ||
                                    argument_node.kind ==
                                        frontend::AstKind::MemberExpression ||
                                    argument_node.kind ==
                                        frontend::AstKind::SubscriptExpression ||
                                    (argument_node.kind ==
                                         frontend::AstKind::UnaryExpression &&
                                     argument_node.text == "*");
                                if (argument_is_lvalue) {
                                    auto reference =
                                        emit_lvalue(*argument_nodes[index]);
                                    if (!reference.value) {
                                        return {};
                                    }
                                    stored = reference.value;
                                } else {
                                    auto element_type =
                                        parameter_type->element
                                            ? parameter_type->element
                                            : int_type();
                                    auto* temporary = builder_.create_alloca(
                                        element_type, 0, "call.operator.temp");
                                    auto held =
                                        emit_expression(*argument_nodes[index]);
                                    if (!held.value) {
                                        return {};
                                    }
                                    builder_.create_store(held.value, temporary);
                                    stored = temporary;
                                }
                                if (stored->type != parameter_type) {
                                    stored = builder_.create(
                                        ir::Opcode::BitCast, parameter_type,
                                        {stored}, "call.operator.reference");
                                }
                                arguments.push_back(stored);
                                continue;
                            }
                            if (parameter_type) {
                                argument.value = promote(argument.value, argument.type,
                                                         parameter_type);
                            }
                            arguments.push_back(argument.value);
                        }
                        auto* call = create_call_instruction(
                            method->second, std::move(arguments),
                            method->second->type->return_type, "call.operator");
                        return reference_result(call, method->second,
                                            method->second->type->return_type);
                    }
                }
            }
        }
    }
    if (expression.children[0]->kind == frontend::AstKind::Identifier && current_class_) {
        const auto methods = class_methods_.find(current_class_->tag);
        if (methods != class_methods_.end()) {
            const auto method = methods->second.find(expression.children[0]->text);
            if (method != methods->second.end()) {
                auto* self = lookup_local("this");
                if (self) {
                    auto* self_value =
                        builder_.create_load(self->type, self->address, false, 0,
                                             "this.call");
                    if (self_value->type != method->second->type->parameters[0]) {
                        self_value = builder_.create(
                            ir::Opcode::BitCast,
                            method->second->type->parameters[0], {self_value},
                            "this.cast");
                    }
                    std::vector<ir::Value*> arguments{self_value};
                    const auto& argument_nodes = expression.children[1]->children;
                    for (std::size_t index = 0; index < argument_nodes.size(); ++index) {
                        if (!argument_nodes[index]) {
                            continue;
                        }
                        auto argument = emit_expression(*argument_nodes[index]);
                        if (!argument.value) {
                            return {};
                        }
                        const auto parameter_index = index + 1;
                        if (parameter_index < method->second->type->parameters.size()) {
                            argument.value = promote(
                                argument.value, argument.type,
                                method->second->type->parameters[parameter_index]);
                        }
                        arguments.push_back(argument.value);
                    }
                    ir::Value* callee = method->second;
                    if (!in_destructor_ && !in_constructor_ && current_class_ &&
                        current_class_->polymorphic &&
                        method_is_virtual(*current_class_,
                                          expression.children[0]->text)) {
                        const int slot = virtual_slot(
                            *current_class_, expression.children[0]->text);
                        if (slot >= 0 && self_value->type &&
                            self_value->type->element) {
                            const auto byte_type = module_->types().int_type(8);
                            auto* zero = module_->constant_int(int_type(), 0);
                            auto* vptr_slot = builder_.create_gep(
                                self_value->type->element, self_value, {zero, zero},
                                false, "this.vptr");
                            vptr_slot->type = module_->types().pointer_type(
                                module_->types().pointer_type(byte_type));
                            auto* table = builder_.create_load(
                                module_->types().pointer_type(byte_type),
                                vptr_slot, false, 0, "this.vtable");
                            auto* function_slot = builder_.create_gep(
                                byte_type, table,
                                {module_->constant_int(
                                    size_type(),
                                    static_cast<std::int64_t>(slot) * static_cast<std::int64_t>(target::get_target(module_->target()).pointer_bits / 8))},
                                false, "this.slot");
                            function_slot->type =
                                module_->types().pointer_type(
                                    module_->types().pointer_type(byte_type));
                            auto* function = builder_.create_load(
                                module_->types().pointer_type(byte_type),
                                function_slot, false, 0, "this.vfn");
                            callee = builder_.create(
                                ir::Opcode::BitCast,
                                module_->types().pointer_type(method->second->type),
                                {function}, "this.vfn.ptr");
                        }
                    }
                    auto* call = create_call_instruction(
                        callee, std::move(arguments),
                        method->second->type->return_type, "this.method");
                    return reference_result(call, method->second,
                                            method->second->type->return_type);
                }
            }
        }
    }
    if (expression.children[0]->kind == frontend::AstKind::Identifier &&
        expression.children[0]->text == "__builtin_va_start") {
        auto* instruction = builder_.create_va_start("vastart");
        return {instruction, instruction->type};
    }
    if (expression.children[0]->kind == frontend::AstKind::MemberExpression) {
        const auto& member = *expression.children[0];
        if (!member.children.empty() && member.children.front()) {
            const bool arrow = !member.labels.empty() && member.labels.front() == "->";
            bool reference_base = false;
            if (!arrow && member.children.front() &&
                member.children.front()->kind == frontend::AstKind::Identifier) {
                if (auto* local =
                        lookup_local(member.children.front()->text)) {
                    reference_base = local->frontend_type &&
                                     local->frontend_type->is_reference;
                }
            }
            Expression base;
            ir::TypePtr record_type = nullptr;
            if (arrow) {
                base = arrow_base(*member.children.front(), record_type);
                if (!base.value) {
                    error(expression.location,
                          "member access on a non-record pointer");
                    return {};
                }
            } else {
                if (member.children.front()->kind ==
                    frontend::AstKind::CallExpression) {
                    // A record value is its address, and a reference-returning
                    // call answers with one, so the member is read through it
                    // rather than through a copy (ADR 0036).
                    auto value = emit_expression(*member.children.front());
                    if (value.value && value.type) {
                        base.value = value.value;
                        base.type = value.type->kind == ir::TypeKind::Pointer &&
                                            value.type->element
                                        ? value.type->element
                                        : value.type;
                        if (base.type && base.type->kind != ir::TypeKind::Struct) {
                            base = {};
                        }
                    }
                } else {
                    base = emit_lvalue(*member.children.front());
                }
                record_type = base.type;
            }
            const auto source_record =
                record_type ? ir_records_.find(record_type) : ir_records_.end();
            ir::Function* method_function = nullptr;
            if (source_record != ir_records_.end()) {
                // A const object calls the const overload of an overload pair,
                // which is the choice Sema made for the same call.
                bool base_is_const = false;
                if (member.children.front()->kind ==
                    frontend::AstKind::Identifier) {
                    const auto* local =
                        lookup_local(member.children.front()->text);
                    if (local != nullptr && local->frontend_type) {
                        base_is_const =
                            local->frontend_type->is_const ||
                            (local->frontend_type->element != nullptr &&
                             local->frontend_type->element->is_const);
                    }
                }
                method_function = find_method(source_record->second->tag,
                                             member.text, base_is_const);
                if (!method_function) {
                    const auto direct =
                        functions_.find(source_record->second->tag + "_" + member.text);
                    if (direct != functions_.end()) {
                        method_function = direct->second;
                    }
                }
            }
            if (method_function && base.value) {
                const auto& method = method_function;
                std::vector<ir::Value*> arguments;
                auto* this_argument = base.value;
                // A member call needs the object's **address**. When the base is a
                // temporary it arrives as a record value, and bitcasting that to a
                // pointer handed the callee garbage -- `Holder().value()` read whatever
                // the load happened to contain. Materialise the value and pass its
                // address.
                if (this_argument && this_argument->type &&
                    this_argument->type->kind == ir::TypeKind::Struct) {
                    auto* storage = builder_.create_alloca(
                        this_argument->type, 0, "temporary.this");
                    builder_.create_store(this_argument, storage);
                    this_argument = storage;
                }
                if (this_argument && this_argument->type &&
                    method->type->parameters[0] != this_argument->type) {
                    this_argument = builder_.create(
                        ir::Opcode::BitCast, method->type->parameters[0], {this_argument},
                        "this.cast");
                }
                arguments.push_back(this_argument);
                const auto& argument_nodes = expression.children[1]->children;
                for (std::size_t index = 0; index < argument_nodes.size(); ++index) {
                    if (!argument_nodes[index]) {
                        continue;
                    }
                    bool reference_parameter = false;
                    const auto ast_type = function_ast_types_.find(method);
                    if (ast_type != function_ast_types_.end() && ast_type->second &&
                        index < ast_type->second->parameters.size() &&
                        ast_type->second->parameters[index] &&
                        ast_type->second->parameters[index]->is_reference) {
                        reference_parameter = true;
                    }
                    Expression argument;
                    if (reference_parameter) {
                        // Only an addressable argument is bound directly; an
                        // rvalue bound to a const reference lives in a temporary.
                        // static_cast<T&&>(x) and static_cast<const T&>(x) name
                        // x, so the parameter binds to it rather than to a copy:
                        // a write through the parameter must reach the caller's
                        // object.
                        const frontend::AstNode* bound = argument_nodes[index].get();
                        if (bound->kind == frontend::AstKind::CastExpression &&
                            bound->children.size() > 1 && bound->children[1]) {
                            bound = bound->children[1].get();
                        }
                        const auto kind = bound->kind;
                        const bool addressable =
                            kind == frontend::AstKind::Identifier ||
                            kind == frontend::AstKind::MemberExpression ||
                            kind == frontend::AstKind::SubscriptExpression ||
                            (kind == frontend::AstKind::UnaryExpression &&
                             bound->text == "*");
                        if (addressable) {
                            argument = emit_lvalue(*bound);
                        }
                        if (!argument.value) {
                            auto value = emit_expression(*argument_nodes[index]);
                            if (value.value && value.type &&
                                value.type->kind == ir::TypeKind::Pointer) {
                                // A reference parameter holds the address the
                                // callee wants; copying the referent would
                                // duplicate the whole object.
                                argument.value = value.value;
                                argument.type = value.type;
                            } else if (value.value && value.type) {
                                auto* storage =
                                    builder_.create_alloca(value.type, 0,
                                                       "argument.temporary");
                                builder_.create_store(value.value, storage);
                                argument = {storage, value.type};
                            }
                        }
                    } else {
                        argument = emit_expression(*argument_nodes[index]);
                    }
                    if (!argument.value) {
                        return {};
                    }
                    if (reference_parameter && argument.value->type) {
                        argument.type = argument.value->type;
                    }
                    const auto parameter_index = index + 1;
                    if (parameter_index < method->type->parameters.size()) {
                        argument.value = promote(
                            argument.value, argument.type,
                            method->type->parameters[parameter_index]);
                    }
                    arguments.push_back(argument.value);
                }
                const auto defaults = function_defaults_.find(method);
                if (defaults != function_defaults_.end()) {
                    for (std::size_t index = arguments.size();
                         index < method->type->parameters.size(); ++index) {
                        const auto default_index = index - 1;
                        if (default_index >= defaults->second.size() ||
                            !defaults->second[default_index]) {
                            break;
                        }
                        auto value = emit_expression(*defaults->second[default_index]);
                        if (!value.value) {
                            break;
                        }
                        arguments.push_back(promote(
                            value.value, value.type, method->type->parameters[index]));
                    }
                }
                ir::Value* callee = method;
                const bool receiver_is_this =
                    expression.children[0] &&
                    expression.children[0]->kind == frontend::AstKind::Identifier &&
                    expression.children[0]->text == "this";
                if (!((in_destructor_ || in_constructor_) &&
                      receiver_is_this) &&
                    (arrow || reference_base) && base.value &&
                    source_record != ir_records_.end() &&
                    method_is_virtual(*source_record->second, member.text)) {
                    const int slot = virtual_slot(*source_record->second, member.text);
                    if (slot >= 0) {
                        const auto byte_type = module_->types().int_type(8);
                        auto* zero = module_->constant_int(int_type(), 0);
                        auto* vptr_slot = builder_.create_gep(
                            record_type, base.value, {zero, zero}, false, "vptr.slot");
                        vptr_slot->type = module_->types().pointer_type(
                            module_->types().pointer_type(byte_type));
                        auto* table = builder_.create_load(
                            module_->types().pointer_type(byte_type), vptr_slot,
                            false, 0, "vtable");
                        auto* function_slot = builder_.create_gep(
                            byte_type, table,
                            {module_->constant_int(
                                size_type(), static_cast<std::int64_t>(slot) * static_cast<std::int64_t>(target::get_target(module_->target()).pointer_bits / 8))},
                            false, "vslot");
                        function_slot->type = module_->types().pointer_type(
                            module_->types().pointer_type(byte_type));
                        auto* function = builder_.create_load(
                            module_->types().pointer_type(byte_type), function_slot,
                            false, 0, "vfn");
                        callee = builder_.create(
                            ir::Opcode::BitCast,
                            module_->types().pointer_type(method->type), {function},
                            "vfn.ptr");
                    }
                }
                auto* call = create_call_instruction(
                    callee, std::move(arguments), method->type->return_type,
                    "method.call");
                return reference_result(call, method, method->type->return_type);
            }
        }
    }
    if (expression.children[0]->kind == frontend::AstKind::Identifier &&
        expression.children[0]->text.find("::") != std::string::npos) {
        const auto separator = expression.children[0]->text.rfind("::");
        const auto class_name = expression.children[0]->text.substr(0, separator);
        const auto method_name = expression.children[0]->text.substr(separator + 2);
        const auto methods = class_methods_.find(class_name);
        if (methods != class_methods_.end()) {
            const auto found = methods->second.find(method_name);
            if (found != methods->second.end() && found->second &&
                !found->second->type->parameters.empty()) {
                ir::Function* method = found->second;
                std::vector<ir::Value*> arguments;
                arguments.push_back(
                    module_->constant_null(method->type->parameters[0]));
                const auto& argument_nodes = expression.children[1]->children;
                for (std::size_t index = 0; index < argument_nodes.size(); ++index) {
                    if (!argument_nodes[index]) {
                        continue;
                    }
                    auto argument = emit_expression(*argument_nodes[index]);
                    if (!argument.value) {
                        return {};
                    }
                    const auto parameter_index = index + 1;
                    if (parameter_index < method->type->parameters.size()) {
                        argument.value = promote(
                            argument.value, argument.type,
                            method->type->parameters[parameter_index]);
                    }
                    arguments.push_back(argument.value);
                }
                auto* call = create_call_instruction(
                    method, std::move(arguments), method->type->return_type,
                    "static.method.call");
                return reference_result(call, method, method->type->return_type);
            }
        }
    }
    // A direct callee is resolved before its expression is lowered: the
    // expression form would emit a function-pointer bitcast the call does not
    // use, and a tile intrinsic must not reference the symbol it replaces.
    ir::Function* direct_function = nullptr;
    if (expression.children[0]->kind == frontend::AstKind::Identifier) {
        // A template callee with a non-int deduction resolved to an
        // instantiated definition at analysis time (ADR 0031).
        const auto target = sema_.call_target(expression);
        direct_function = lookup_function(target.empty()
                                           ? expression.children[0]->text
                                           : std::string_view(target));
    }
    if (direct_function) {
        const auto intrinsic = matrix_intrinsic(*direct_function);
        if (intrinsic != ir::Intrinsic::None) {
            return emit_matrix_intrinsic(expression, *direct_function, intrinsic);
        }
    }
    Expression callee;
    if (direct_function) {
        callee = {direct_function, direct_function->type};
    } else {
        callee = emit_expression(*expression.children[0]);
    }
    if (!callee.value) {
        return {};
    }
    ir::TypePtr function_type = callee.type;
    if (function_type && function_type->kind == ir::TypeKind::Pointer) {
        function_type = function_type->element;
    }
    if (!function_type || function_type->kind != ir::TypeKind::Function) {
        error(expression.location, "called object is not a function");
        return {};
    }
    std::vector<ir::Value*> arguments;
    const auto& argument_nodes = expression.children[1]->children;
    const auto convert_record_argument =
        [&](Expression argument, const frontend::AstNode& node,
            ir::TypePtr target) -> Expression {
        if (!argument.type || !target) {
            return argument;
        }
        if (target->kind == ir::TypeKind::Struct &&
            argument.type->kind != ir::TypeKind::Struct) {
            const auto record = ir_records_.find(target);
            if (record != ir_records_.end() && record->second) {
                auto* constructor =
                    select_constructor(record->second->tag, 1, 0);
                if (constructor && constructor->type &&
                    constructor->type->parameters.size() == 2) {
                    auto value = emit_expression(node);
                    if (value.value) {
                        value.value = promote(
                            value.value, value.type,
                            constructor->type->parameters[1]);
                        auto* storage = builder_.create_alloca(
                            target, 0, "argument.temp");
                        auto* self = storage;
                        if (self->type !=
                            constructor->type->parameters[0]) {
                            self = builder_.create(
                                ir::Opcode::BitCast,
                                constructor->type->parameters[0], {self},
                                "argument.this");
                        }
                        create_call_instruction(
                            constructor, {self, value.value}, nullptr,
                            "argument.constructor");
                        auto* loaded = builder_.create_load(
                            target, storage, false, 0, "argument.record");
                        return {loaded, target};
                    }
                }
            }
        }
        if (argument.type->kind != ir::TypeKind::Struct ||
            (!ir::is_integer(target) && !ir::is_float(target))) {
            return argument;
        }
        const auto record = ir_records_.find(argument.type);
        if (record == ir_records_.end() || !record->second) {
            return argument;
        }
        const auto methods = class_methods_.find(record->second->tag);
        if (methods == class_methods_.end()) {
            return argument;
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
            return argument;
        }
        Expression self;
        if (node.kind == frontend::AstKind::CallExpression) {
            auto* storage = builder_.create_alloca(argument.type, 0,
                                                       "argument.temp");
            builder_.create_store(argument.value, storage);
            self.value = storage;
            self.type = argument.type;
        } else {
            self = emit_lvalue(node);
        }
        if (!self.value) {
            return argument;
        }
        if (self.value->type != method->second->type->parameters[0]) {
            self.value = builder_.create(
                ir::Opcode::BitCast, method->second->type->parameters[0],
                {self.value}, "argument.this");
        }
        auto* call = create_call_instruction(
            method->second, {self.value}, method->second->type->return_type,
            "argument.conversion");
        return reference_result(call, method->second,
                                method->second->type->return_type);
    };
    // A memory-class aggregate travels as a pointer to a caller-owned copy, and that
    // copy must come from the argument's **address**: a record value is carried as one
    // machine word, so storing the loaded value loses everything past it -- a
    // three-byte record arrived as two bytes on KR16 while KR32's four-byte word hid
    // it. `copy_record_fields` is the field-by-field copy that keeps every byte.
    ir::Function* metadata_function = direct_function;
    if (!metadata_function) {
        metadata_function = dynamic_cast<ir::Function*>(callee.value);
    }
    for (std::size_t index = 0; index < argument_nodes.size(); ++index) {
        if (!argument_nodes[index]) {
            continue;
        }
        bool reference_parameter = false;
        if (metadata_function) {
            const auto ast_type = function_ast_types_.find(metadata_function);
            if (ast_type != function_ast_types_.end() && ast_type->second &&
                index < ast_type->second->parameters.size() &&
                ast_type->second->parameters[index] &&
                ast_type->second->parameters[index]->is_reference) {
                reference_parameter = true;
            }
        }
        Expression argument;
        if (reference_parameter) {
            if (argument_nodes[index]->kind ==
                frontend::AstKind::CallExpression) {
                auto value = emit_expression(*argument_nodes[index]);
                if (value.value && value.type &&
                    value.type->kind == ir::TypeKind::Struct) {
                    auto* storage = builder_.create_alloca(
                        value.type, 0, "argument.reference");
                    builder_.create_store(value.value, storage);
                    argument.value = storage;
                    argument.type = value.type;
                } else if (value.value && value.type &&
                           value.type->kind == ir::TypeKind::Pointer) {
                    // A call returning a reference already yields the address the
                    // parameter wants, so it is bound directly. Leaving the argument
                    // empty here dropped the *outer* call entirely: `head(pick(x))`
                    // emitted `pick` and never `head`, and the comparison consuming
                    // it then had a null operand.
                    argument.value = value.value;
                    argument.type = value.type;
                }
            } else {
                // Only an addressable argument is bound directly; an rvalue
                // bound to a const reference lives in a temporary.
                // The same binding rule as a member call: a cast to a reference
                // names the operand, so the parameter aliases it.
                const frontend::AstNode* bound = argument_nodes[index].get();
                if (bound->kind == frontend::AstKind::CastExpression &&
                    bound->children.size() > 1 && bound->children[1]) {
                    bound = bound->children[1].get();
                }
                const auto kind = bound->kind;
                const bool addressable =
                    kind == frontend::AstKind::Identifier ||
                    kind == frontend::AstKind::MemberExpression ||
                    kind == frontend::AstKind::SubscriptExpression ||
                    (kind == frontend::AstKind::UnaryExpression &&
                     bound->text == "*");
                if (addressable) {
                    argument = emit_lvalue(*bound);
                }
                if (!argument.value) {
                    auto value = emit_expression(*argument_nodes[index]);
                    if (value.value && value.type &&
                        value.type->kind == ir::TypeKind::Pointer) {
                        argument.value = value.value;
                        argument.type = value.type;
                    } else if (value.value && value.type) {
                        auto* storage = builder_.create_alloca(value.type, 0,
                                                             "argument.temporary");
                        builder_.create_store(value.value, storage);
                        argument = {storage, value.type};
                    }
                }
            }
        } else {
            argument = emit_expression(*argument_nodes[index]);
        }
        if (!argument.value) {
            return {};
        }
        if (reference_parameter && argument.value->type) {
            argument.type = argument.value->type;
        }
        if (index < function_type->parameters.size()) {
            const auto parameter_type = function_type->parameters[index];
            // A reference parameter already holds the address its caller wants
            // to pass on; loading the referent and copying it would duplicate a
            // whole object for every call.
            const auto* argument_node = argument_nodes[index].get();
            const auto* reference_local =
                argument_node->kind == frontend::AstKind::Identifier
                    ? lookup_local(argument_node->text)
                    : nullptr;
            if (parameter_type && parameter_type->kind == ir::TypeKind::Pointer &&
                parameter_type->element &&
                parameter_type->element->kind == ir::TypeKind::Struct &&
                reference_local && reference_local->frontend_type &&
                reference_local->frontend_type->is_reference) {
                auto direct = emit_expression(*argument_node);
                if (direct.value) {
                    arguments.push_back(
                        promote(direct.value, direct.type, parameter_type));
                    continue;
                }
            }
            // A by-value memory-class aggregate: build the caller's copy here, from
            // the argument's address, and pass the copy. The pointer type means
            // create_call_instruction leaves it alone instead of storing the value
            // as one word.
            if (!reference_parameter && parameter_type &&
                parameter_type->kind == ir::TypeKind::Pointer &&
                parameter_type->element &&
                parameter_type->element->kind == ir::TypeKind::Struct &&
                argument.type && argument.type->kind == ir::TypeKind::Struct) {
                const auto& node = *argument_nodes[index];
                const bool addressable =
                    node.kind == frontend::AstKind::Identifier ||
                    node.kind == frontend::AstKind::MemberExpression ||
                    node.kind == frontend::AstKind::SubscriptExpression ||
                    (node.kind == frontend::AstKind::UnaryExpression &&
                     node.text == "*");
                ir::Value* source = nullptr;
                if (addressable) {
                    const auto reference = emit_lvalue(node);
                    if (reference.value && reference.value->type &&
                        reference.value->type->kind == ir::TypeKind::Pointer &&
                        reference.value->type->element == argument.type) {
                        source = reference.value;
                    }
                }
                auto* copy = builder_.create_alloca(argument.type, 0, "argument.copy");
                if (source != nullptr) {
                    copy_record_fields(source, copy, argument.type);
                } else {
                    // No address to read from -- a call returning a record, which
                    // already arrives as one word. The store keeps the previous
                    // behaviour rather than inventing a path.
                    builder_.create_store(argument.value, copy);
                }
                argument.value =
                    copy->type == parameter_type
                        ? static_cast<ir::Value*>(copy)
                        : builder_.create(ir::Opcode::BitCast, parameter_type, {copy},
                                          "argument.pointer");
                argument.type = parameter_type;
                arguments.push_back(argument.value);
                continue;
            }
            argument = convert_record_argument(argument, *argument_nodes[index],
                                               function_type->parameters[index]);
            argument.value = promote(argument.value, argument.type,
                                     function_type->parameters[index]);
            argument.type = function_type->parameters[index];
        } else if (ir::is_integer(argument.type) &&
                   argument.type->bits < profile_.int_bits) {
            const auto promoted = module_->types().int_type(profile_.int_bits, true);
            argument.value = promote(argument.value, argument.type, promoted);
            argument.type = promoted;
        } else if (ir::is_float(argument.type) && argument.type->bits < 64) {
            const auto promoted = module_->types().float_type(64);
            argument.value = promote(argument.value, argument.type, promoted);
            argument.type = promoted;
        }
        arguments.push_back(argument.value);
    }
    if (direct_function) {
        const auto defaults = function_defaults_.find(direct_function);
        if (defaults != function_defaults_.end()) {
            for (std::size_t index = arguments.size();
                 index < function_type->parameters.size(); ++index) {
                if (index >= defaults->second.size() || !defaults->second[index]) {
                    break;
                }
                auto value = emit_expression(*defaults->second[index]);
                if (!value.value) {
                    break;
                }
                arguments.push_back(
                    promote(value.value, value.type, function_type->parameters[index]));
            }
        }
    }
    auto* call = create_call_instruction(callee.value, arguments, function_type->return_type,
                                      "call");
    return reference_result(call, direct_function, function_type->return_type);
}

CIRGen::Expression CIRGen::emit_member_lvalue(const frontend::AstNode& expression) {
    if (expression.children.empty() || !expression.children.front()) {
        return {};
    }
    const bool arrow = !expression.labels.empty() && expression.labels.front() == "->";
    Expression base;
    ir::TypePtr record_type = nullptr;
    ir::Value* record_address = nullptr;
    const frontend::RecordInfo* source_record = nullptr;
    if (arrow) {
        base = arrow_base(*expression.children.front(), record_type);
        if (!base.value) {
            error(expression.location, "member access on a non-record pointer");
            return {};
        }
        record_address = base.value;
        if (expression.children.front()->kind ==
            frontend::AstKind::Identifier) {
            frontend::TypePtr pointer_frontend;
            if (auto* local = lookup_local(
                    expression.children.front()->text)) {
                pointer_frontend = local->frontend_type;
            } else {
                const auto found = globals_.find(
                    expression.children.front()->text);
                if (found != globals_.end()) {
                    const auto frontend =
                        global_frontend_types_.find(found->second);
                    if (frontend != global_frontend_types_.end()) {
                        pointer_frontend = frontend->second;
                    }
                }
            }
            if (pointer_frontend &&
                pointer_frontend->kind == frontend::TypeKind::Pointer &&
                pointer_frontend->element &&
                pointer_frontend->element->is_volatile) {
                base.is_volatile = true;
            }
        }
        const auto source = ir_records_.find(record_type);
        if (source != ir_records_.end()) {
            source_record = source->second;
        }
    } else {
        if (expression.children.front()->kind ==
            frontend::AstKind::CallExpression) {
            // A record value is its address here, and a reference-returning
            // call answers with one, so the member is read through the object
            // the call named (ADR 0036).
            auto value = emit_expression(*expression.children.front());
            if (value.value && value.type) {
                base.value = value.value;
                base.type = value.type->kind == ir::TypeKind::Pointer &&
                                    value.type->element
                                ? value.type->element
                                : value.type;
            }
        } else {
            base = emit_lvalue(*expression.children.front());
        }
        if (!base.value || !base.type || base.type->kind != ir::TypeKind::Struct) {
            error(expression.location, "member access on a non-record value");
            return {};
        }
        record_type = base.type;
        record_address = base.value;
        const auto source = ir_records_.find(record_type);
        if (source != ir_records_.end()) {
            source_record = source->second;
        }
    }

    if (source_record) {
        const frontend::RecordInfo* selected_record = source_record;
        std::size_t field_index = selected_record->members.size();
        for (auto* current = source_record; current != nullptr;
             current = current->base && current->base->record
                           ? current->base->record.get()
                           : nullptr) {
            for (std::size_t index = 0; index < current->members.size();
                 ++index) {
                if (current->members[index].name == expression.text) {
                    field_index = index;
                    selected_record = current;
                    break;
                }
            }
            if (field_index < current->members.size()) {
                break;
            }
        }
        const auto& record = *selected_record;
        if (field_index >= record.members.size() || !record.members[field_index].type) {
            error(expression.location, "no member named '" + expression.text + "'");
            return {};
        }
        const auto& member = record.members[field_index];
        const auto offsets = sema_.record_member_offsets(record);
        const auto field_type = map_type(member.type);
        if (!field_type) {
            error(expression.location, "cannot map bit-field member type");
            return {};
        }
        auto* byte_type = module_->types().int_type(8, false);
        auto* byte_pointer = builder_.create(ir::Opcode::BitCast,
                                             module_->types().pointer_type(byte_type),
                                             {record_address}, "member.byte");
        auto* offset = module_->constant_int(int_type(),
                                             static_cast<std::int64_t>(offsets[field_index]));
        auto* address = builder_.create_gep(byte_type, byte_pointer, {offset}, false,
                                            "member");
        address->type = module_->types().pointer_type(field_type);
        Expression result{address, field_type};
        result.reference_member = member.type->is_reference;
        result.is_volatile = base.is_volatile || member.type->is_volatile;
        if (member.bit_field && member.bit_width > 0) {
            std::uint32_t bit_offset = 0;
            for (std::size_t index = 0; index < field_index; ++index) {
                if (record.members[index].bit_field &&
                    offsets[index] == offsets[field_index] &&
                    record.members[index].bit_width > 0) {
                    bit_offset += record.members[index].bit_width;
                }
            }
            result.bit_field = true;
            result.bit_offset = bit_offset;
            result.bit_width = member.bit_width;
        }
        return result;
    }

    std::size_t field_index = record_type->field_names.size();
    for (std::size_t index = 0; index < record_type->field_names.size(); ++index) {
        if (record_type->field_names[index] == expression.text) {
            field_index = index;
            break;
        }
    }
    if (field_index >= record_type->fields.size()) {
        error(expression.location, "no member named '" + expression.text + "'");
        return {};
    }
    auto* zero = module_->constant_int(int_type(), 0);
    auto* index_value = module_->constant_int(int_type(),
                                              static_cast<std::int64_t>(field_index));
    auto* address = builder_.create_gep(record_type, record_address, {zero, index_value},
                                        false, "member");
    address->type = module_->types().pointer_type(record_type->fields[field_index]);
    Expression result{address, record_type->fields[field_index]};
    result.is_volatile = base.is_volatile;
    return result;
}

CIRGen::Expression CIRGen::emit_implicit_member_lvalue(std::string_view name) {
    auto* self = lookup_local("this");
    if (!current_class_ || !self || !self->type || self->type->kind != ir::TypeKind::Pointer) {
        return {};
    }
    std::size_t index = current_class_->members.size();
    const frontend::RecordInfo* owner = current_class_;
    for (auto* current = current_class_; current != nullptr;
         current = current->base && current->base->record
                       ? current->base->record.get()
                       : nullptr) {
        for (std::size_t member_index = 0; member_index < current->members.size();
             ++member_index) {
            if (current->members[member_index].name == name) {
                index = member_index;
                owner = current;
                break;
            }
        }
        if (index < current->members.size()) {
            break;
        }
    }
    if (index >= owner->members.size() || !owner->members[index].type) {
        return {};
    }
    // A polymorphic record's IR struct carries hidden fields (the vptr, and the
    // base subobject), so its field index is not the frontend member index. Map
    // the member type directly; the byte offset below already accounts for them.
    const auto field_type = map_type(owner->members[index].type);
    if (!field_type) {
        return {};
    }
    const auto offsets = sema_.record_member_offsets(*owner);
    auto* self_value = current_this_;
    if (self_value == nullptr) {
        self_value =
            builder_.create_load(self->type, self->address, false, 0, "this.load");
    }
    auto* byte_type = module_->types().int_type(8, false);
    auto* byte_pointer = current_this_byte_;
    if (byte_pointer == nullptr) {
        byte_pointer = builder_.create(
            ir::Opcode::BitCast, module_->types().pointer_type(byte_type), {self_value},
            "this.byte");
    }
    auto* offset =
        module_->constant_int(int_type(), static_cast<std::int64_t>(offsets[index]));
    auto* address =
        builder_.create_gep(byte_type, byte_pointer, {offset}, false, "this.member");
    address->type = module_->types().pointer_type(field_type);
    Expression result{address, field_type};
    result.reference_member = owner->members[index].type->is_reference;
    return result;
}

bool CIRGen::method_is_virtual(const frontend::RecordInfo& record,
                               std::string_view name) const {
    for (auto* current = &record; current != nullptr;
         current = current->base && current->base->record
                       ? current->base->record.get()
                       : nullptr) {
        for (const auto& method : current->methods) {
            if (method.name == name && method.is_virtual) {
                return true;
            }
        }
    }
    return false;
}

std::vector<std::string> CIRGen::virtual_slots(
    const frontend::RecordInfo& record) const {
    std::vector<std::string> slots;
    if (record.base && record.base->record) {
        slots = virtual_slots(*record.base->record);
    }
    for (const auto& method : record.methods) {
        if (!method.is_virtual) {
            continue;
        }
        if (method.name.starts_with("~")) {
            const bool has_destructor =
                std::any_of(slots.begin(), slots.end(), [](const std::string& slot) {
                    return slot.starts_with("~");
                });
            if (!has_destructor) {
                slots.push_back(method.name);
            }
            continue;
        }
        if (std::find(slots.begin(), slots.end(), method.name) == slots.end()) {
            slots.push_back(method.name);
        }
    }
    return slots;
}

int CIRGen::virtual_slot(const frontend::RecordInfo& record,
                         std::string_view name) const {
    const auto slots = virtual_slots(record);
    for (std::size_t index = 0; index < slots.size(); ++index) {
        if (slots[index] == name) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

// The spelling a scalar's descriptor is keyed and named by. Only the kinds the
// language has without a class are covered, which is what `typeid(int)` needs.
std::string scalar_type_name(const frontend::Type& type) {
    auto name = std::string{};
    switch (type.kind) {
        case frontend::TypeKind::Void:
            name = "void";
            break;
        case frontend::TypeKind::Bool:
            name = "bool";
            break;
        case frontend::TypeKind::Char:
            name = "char";
            break;
        case frontend::TypeKind::Int:
            // The language has one integer kind here, so the spelling is the
            // common one; a distinct long descriptor is not distinguished.
            name = "int";
            break;
        case frontend::TypeKind::Float:
            name = "float";
            break;
        case frontend::TypeKind::Double:
            name = "double";
            break;
        case frontend::TypeKind::Pointer:
            name = "pointer";
            break;
        default:
            name = "unknown";
            break;
    }
    // The spelling is also the symbol suffix, so it carries no space: the KR16
    // assembler rejects a symbol it cannot lex.
    if (type.is_unsigned && name != "pointer" && name != "unknown") {
        name = "unsigned_" + name;
    }
    return name;
}
ir::GlobalVariable* CIRGen::ensure_scalar_typeinfo(std::string_view name) {
    const auto key = std::string(name);
    const auto cached = scalar_typeinfos_.find(key);
    if (cached != scalar_typeinfos_.end() && cached->second) {
        return cached->second;
    }
    const auto i32 = int_type();
    std::vector<std::uint8_t> name_bytes(key.begin(), key.end());
    name_bytes.push_back(0);
    const auto name_type = module_->types().array_type(
        module_->types().int_type(8), name_bytes.size());
    auto* type_name = module_->add_global("_ZTS" + key, name_type, true, 1);
    type_name->weak = true;
    type_name->initializer = module_->constant_data_array(name_type, std::move(name_bytes));

    // The same layout a class descriptor has: vtable pointer, name pointer,
    // flags, and a base word that stays zero because a scalar has no base.
    const auto words = module_->types().array_type(i32, 4);
    auto* typeinfo = module_->add_global("_ZTI" + key, words, true, 4);
    typeinfo->weak = true;
    scalar_typeinfos_[key] = typeinfo;
    auto* zero = module_->constant_int(i32, 0);
    typeinfo->initializer =
        module_->constant_address_array(words, {zero, type_name, zero, zero});
    return typeinfo;
}
ir::GlobalVariable* CIRGen::ensure_typeinfo(const frontend::RecordInfo& record) {
    const auto cached = typeinfo_globals_.find(&record);
    if (cached != typeinfo_globals_.end() && cached->second) {
        return cached->second;
    }
    const auto i32 = int_type();
    std::vector<std::uint8_t> name_bytes(record.tag.begin(), record.tag.end());
    name_bytes.push_back(0);
    const auto name_type = module_->types().array_type(
        module_->types().int_type(8), name_bytes.size());
    auto* name = module_->add_global(type_symbol(record, "_ZTS"), name_type, true, 1);
    name->weak = true;
    name->initializer = module_->constant_data_array(name_type, std::move(name_bytes));

    // type_info layout: vtable pointer, name pointer, flags. A class that
    // derives from another adds the base typeinfo as a fourth word, the
    // single-inheritance form, so dynamic_cast can walk the chain.
    const auto words = module_->types().array_type(i32, 4);
    auto* typeinfo = module_->add_global(type_symbol(record, "_ZTI"), words, true, 4);
    typeinfo->weak = true;
    typeinfo_globals_[&record] = typeinfo;
    ir::GlobalVariable* base = nullptr;
    if (record.base && record.base->kind == frontend::TypeKind::Record &&
        record.base->record) {
        base = ensure_typeinfo(*record.base->record);
    }
    auto* zero = module_->constant_int(i32, 0);
    typeinfo->initializer = module_->constant_address_array(
        words, {zero, name, zero,
                base ? static_cast<ir::Value*>(base) : static_cast<ir::Value*>(zero)});
    return typeinfo;
}

ir::GlobalVariable* CIRGen::ensure_vtable(const frontend::RecordInfo& record) {
    const auto cached = vtable_globals_.find(&record);
    if (cached != vtable_globals_.end() && cached->second) {
        return cached->second;
    }
    const auto slots = virtual_slots(record);
    if (slots.empty()) {
        return nullptr;
    }
    // Word 0 is the typeinfo address and the object's vptr points at word 1,
    // so virtual slot k is at vptr + 4*k and the RTTI pointer at vptr - 4.
    const auto count = slots.size() + 1;
    const auto table_type = module_->types().array_type(int_type(), count);
    auto* table = module_->add_global(type_symbol(record, "_ZTV"), table_type, true, 4);
    table->weak = true;
    table->initializer = module_->constant_address_array(
        table_type, std::vector<ir::Value*>(count, module_->constant_int(int_type(), 0)));
    vtable_globals_[&record] = table;
    pending_vtables_.push_back({&record, table});
    return table;
}

ir::Function* CIRGen::vtable_slot_implementation(const frontend::RecordInfo& record,
                                                 std::string_view slot) {
    const bool destructor_slot = slot.starts_with("~");
    for (auto* current = &record; current != nullptr;
         current = current->base && current->base->record
                       ? current->base->record.get()
                       : nullptr) {
        if (destructor_slot) {
            if (auto* implementation = ensure_member_destructor(current)) {
                return implementation;
            }
        }
        const auto methods = class_methods_.find(current->tag);
        if (methods == class_methods_.end()) {
            continue;
        }
        const auto found = methods->second.find(std::string(slot));
        if (found != methods->second.end()) {
            if (found->second->declaration) {
                for (const auto& method : current->methods) {
                    if (method.name == slot && method.is_pure) {
                        return nullptr;
                    }
                }
            }
            // A declaration may be defined in another translation unit. The
            // vtable references it so the linker resolves the slot; treating it
            // as absent emitted a null slot and made the call jump to zero.
            return found->second;
        }
        if (destructor_slot) {
            for (const auto& candidate : methods->second) {
                if (candidate.first.starts_with("~")) {
                    return candidate.second;
                }
            }
        }
    }
    return nullptr;
}

void CIRGen::finalize_vtables() {
    for (auto& pending : pending_vtables_) {
        if (!pending.record || !pending.table || !pending.table->initializer) {
            continue;
        }
        auto* words = static_cast<ir::ConstantAddressArray*>(pending.table->initializer);
        if (words->elements.empty()) {
            continue;
        }
        words->elements[0] = ensure_typeinfo(*pending.record);
        const auto slots = virtual_slots(*pending.record);
        for (std::size_t index = 0; index < slots.size() &&
                                    index + 1 < words->elements.size();
             ++index) {
            if (auto* implementation =
                    vtable_slot_implementation(*pending.record, slots[index])) {
                words->elements[index + 1] = implementation;
            } else {
                words->elements[index + 1] = module_->constant_int(int_type(), 0);
            }
        }
    }
}

void CIRGen::emit_vtable_init(ir::Value* object, ir::TypePtr type) {
    if (!object || !type || type->kind != ir::TypeKind::Struct) {
        return;
    }
    const auto entry = ir_records_.find(type);
    if (entry == ir_records_.end() || !entry->second ||
        !entry->second->polymorphic) {
        return;
    }
    auto* table = ensure_vtable(*entry->second);
    if (!table) {
        return;
    }
    const auto byte_type = module_->types().int_type(8);
    auto* table_pointer = builder_.create(
        ir::Opcode::BitCast, module_->types().pointer_type(byte_type), {table},
        "vtable");
    // The vptr points at the first virtual entry, which is one pointer past
    // the descriptor slot -- and a pointer is not always four bytes. A literal 4
    // here put the vptr two words into a KR16 vtable, so every dynamic read
    // landed one slot off (ADR 0037).
    const auto descriptor_bytes = static_cast<std::int64_t>(
        target::get_target(module_->target()).pointer_bits / 8);
    auto* functions = builder_.create_gep(
        byte_type, table_pointer,
        {module_->constant_int(int_type(), descriptor_bytes)}, false,
        "vtable.functions");
    auto* zero = module_->constant_int(int_type(), 0);
    auto* vptr_slot = builder_.create_gep(type, object, {zero, zero}, false,
                                          "vptr.slot");
    vptr_slot->type = module_->types().pointer_type(
        module_->types().pointer_type(byte_type));
    builder_.create_store(functions, vptr_slot);
}

CIRGen::Expression CIRGen::emit_implicit_member(std::string_view name) {
    auto address = emit_implicit_member_lvalue(name);
    if (!address.value || !address.type) {
        return {};
    }
    if (address.type->kind == ir::TypeKind::Array) {
        auto* zero = module_->constant_int(int_type(), 0);
        auto* decay = builder_.create_gep(address.type, address.value, {zero, zero},
                                          false, std::string(name) + ".decay");
        decay->type = module_->types().pointer_type(address.type->element);
        return {decay, decay->type};
    }
    if (address.reference_member && address.type &&
        address.type->kind == ir::TypeKind::Pointer && address.type->element) {
        auto* pointer = builder_.create_load(address.type, address.value, false, 0,
                                             std::string(name) + ".ref");
        return {builder_.create_load(address.type->element, pointer, false, 0,
                                     std::string(name) + ".ref.load"),
                address.type->element};
    }
    return {builder_.create_load(address.type, address.value, false, 0,
                                 std::string(name) + ".load"),
            address.type};
}

ir::Function* CIRGen::find_method(const std::string& tag,
                                       const std::string& name,
                                       bool want_const) const {
    const auto methods = class_methods_.find(tag);
    if (methods == class_methods_.end()) {
        return nullptr;
    }
    const auto& table = methods->second;
    const auto const_key = name + "#const";
    if (want_const) {
        const auto found = table.find(const_key);
        if (found != table.end() && found->second) {
            return found->second;
        }
    }
    const auto plain = table.find(name);
    if (plain != table.end() && plain->second) {
        return plain->second;
    }
    const auto only = table.find(const_key);
    return only != table.end() ? only->second : nullptr;
}

CIRGen::Expression CIRGen::emit_member(const frontend::AstNode& expression) {    auto address = emit_member_lvalue(expression);
    if (!address.value) {
        return {};
    }
    if (address.bit_field) {
        if (!address.value->type || !address.value->type->element) {
            error(expression.location, "bit-field has no storage type");
            return {};
        }
        auto* loaded = builder_.create_load(address.type, address.value,
                                            address.is_volatile, 0,
                                            "bitfield.load");
        auto* shift = module_->constant_int(address.type, address.bit_offset);
        auto* shifted = builder_.create_lshr(loaded, shift, "bitfield.shift");
        const auto width = address.bit_width;
        const auto field_bits = address.type->bits;
        std::uint64_t mask = width >= 64 ? ~0ull : ((1ull << width) - 1);
        auto* mask_value = module_->constant_int(address.type, mask);
        auto* masked = builder_.create_and(shifted, mask_value, "bitfield.mask");
        if (address.type->is_signed && width < field_bits) {
            auto* left_shift = module_->constant_int(address.type, field_bits - width);
            auto* left = builder_.create_shl(masked, left_shift, "bitfield.sign");
            auto* right = builder_.create_ashr(left, left_shift, "bitfield.extend");
            return {right, address.type};
        }
        return {masked, address.type};
    }
    if (address.type && address.type->kind == ir::TypeKind::Array) {
        auto* zero = module_->constant_int(int_type(), 0);
        auto* decay = builder_.create_gep(address.type, address.value, {zero, zero}, false,
                                          "member.decay");
        decay->type = module_->types().pointer_type(address.type->element);
        return {decay, decay->type};
    }
    if (address.reference_member && address.type &&
        address.type->kind == ir::TypeKind::Pointer && address.type->element) {
        auto* pointer = builder_.create_load(address.type, address.value, false, 0,
                                             "member.reference");
        return {builder_.create_load(address.type->element, pointer, false, 0,
                                     "member.reference.load"),
                address.type->element};
    }
    if (!address.type) {
        error(expression.location, "member type is unknown");
        return {};
    }
    // An aggregate member is a value like any other: the backend copies it out
    // of the object into the value's frame slot.
    const auto aggregate = address.type->kind == ir::TypeKind::Struct ||
                           address.type->kind == ir::TypeKind::Union;
    return {builder_.create_load(address.type, address.value,
                                 address.is_volatile, 0,
                                 aggregate ? "member.aggregate" : "member.load"),
            address.type};
}

// Calls a member operator with the already-emitted object address, so a
// binary operator resolves the same way whether its left operand is a name or
// an expression such as `*this`.
// Calls a member operator that takes no argument, such as `operator*`, on an
// already-emitted object address. `as_address` returns the address the operator
// produced when it returns a reference, which is what an lvalue context needs.
CIRGen::Expression CIRGen::unary_operator_call(ir::Function* method_function,
                                               Expression self,
                                               bool as_address) {
    if (!self.value || method_function->type->parameters.empty()) {
        return {};
    }
    if (self.value->type != method_function->type->parameters[0]) {
        self.value = builder_.create(ir::Opcode::BitCast,
                                     method_function->type->parameters[0],
                                     {self.value}, "op.this");
    }
    auto* call = create_call_instruction(method_function, {self.value},
                                         method_function->type->return_type,
                                         "operator.call");
    const auto returned = method_function->type->return_type;
    if (returned && returned->kind == ir::TypeKind::Pointer && returned->element) {
        if (as_address) {
            return {call, returned->element};
        }
        return {builder_.create_load(returned->element, call, false, 0,
                                     "operator.load"),
                returned->element};
    }
    return {call, returned};
}

// A record that overloads `operator*` answers a dereference with that member;
// `nullptr` means the operand is not such a record.
ir::Function* CIRGen::dereference_overload(const Expression& object) {
    if (!object.value || !object.type) {
        return nullptr;
    }
    const auto record = ir_records_.find(object.type);
    if (record == ir_records_.end() || !record->second) {
        return nullptr;
    }
    const auto methods = class_methods_.find(record->second->tag);
    if (methods == class_methods_.end()) {
        return nullptr;
    }
    const auto method = methods->second.find("operator*");
    return method == methods->second.end() ? nullptr : method->second;
}

// A record that overloads `operator->` answers `owner->member` with that member;
// `nullptr` means the operand is not such a record.
ir::Function* CIRGen::arrow_overload(const Expression& object) {
    if (!object.value || !object.type) {
        return nullptr;
    }
    const auto record = ir_records_.find(object.type);
    if (record == ir_records_.end() || !record->second) {
        return nullptr;
    }
    const auto methods = class_methods_.find(record->second->tag);
    if (methods == class_methods_.end()) {
        return nullptr;
    }
    const auto method = methods->second.find("operator->");
    return method == methods->second.end() ? nullptr : method->second;
}

// Resolves the base of `a->member`: a pointer to a record, or the call an
// overloaded `operator->` makes. `record_type` receives the record whose member
// is named. Returns an empty expression when the base is neither.
CIRGen::Expression CIRGen::arrow_base(const frontend::AstNode& operand,
                                      ir::TypePtr& record_type) {
    auto base = emit_expression(operand);
    if (!base.value || !base.type) {
        return {};
    }
    if (base.type->kind == ir::TypeKind::Pointer && base.type->element &&
        base.type->element->kind == ir::TypeKind::Struct) {
        record_type = base.type->element;
        return base;
    }
    auto* overload = arrow_overload(base);
    if (overload == nullptr || overload->type->parameters.empty()) {
        return {};
    }
    auto object = emit_lvalue(operand);
    if (!object.value) {
        return {};
    }
    if (object.value->type != overload->type->parameters[0]) {
        object.value = builder_.create(ir::Opcode::BitCast,
                                       overload->type->parameters[0],
                                       {object.value}, "arrow.this");
    }
    base.value = create_call_instruction(overload, {object.value},
                                         overload->type->return_type,
                                         "arrow.call");
    base.type = overload->type->return_type;
    if (base.type && base.type->kind == ir::TypeKind::Pointer &&
        base.type->element && base.type->element->kind == ir::TypeKind::Struct) {
        record_type = base.type->element;
        return base;
    }
    return {};
}

CIRGen::Expression CIRGen::binary_operator_call(ir::Function* method_function,
                                                Expression self,
                                                const frontend::AstNode& right) {
    const auto ast_type = function_ast_types_.find(method_function);
    const bool reference_argument =
        ast_type != function_ast_types_.end() && ast_type->second &&
        !ast_type->second->parameters.empty() &&
        ast_type->second->parameters.front() &&
        ast_type->second->parameters.front()->is_reference;
    auto argument = reference_argument ? emit_lvalue(right) : emit_expression(right);
    if (!self.value || !argument.value) {
        return {};
    }
    if (reference_argument && argument.value->type) {
        argument.type = argument.value->type;
    }
    if (self.value->type != method_function->type->parameters[0]) {
        self.value = builder_.create(ir::Opcode::BitCast,
                                     method_function->type->parameters[0],
                                     {self.value}, "op.this");
    }
    if (method_function->type->parameters.size() > 1) {
        argument.value = promote(argument.value, argument.type,
                                 method_function->type->parameters[1]);
    }
    auto* call = create_call_instruction(
        method_function, {self.value, argument.value},
        method_function->type->return_type, "operator.call");
    return {call, method_function->type->return_type};
}
CIRGen::Expression CIRGen::subscript_operator(ir::Function* function,
                                              Expression self,
                                              const frontend::AstNode& index_node,
                                              bool as_address) {
    auto index = emit_expression(index_node);
    if (!self.value || !index.value) {
        return {};
    }
    if (self.value->type != function->type->parameters[0]) {
        self.value = builder_.create(ir::Opcode::BitCast,
                                     function->type->parameters[0], {self.value},
                                     "subscript.this");
    }
    std::vector<ir::Value*> arguments{self.value};
    if (index.value->type != function->type->parameters[1]) {
        index.value = promote(index.value, index.type, function->type->parameters[1]);
    }
    arguments.push_back(index.value);
    auto* call = create_call_instruction(function, std::move(arguments),
                                         function->type->return_type, "subscript.call");
    if (function->type->return_type &&
        function->type->return_type->kind != ir::TypeKind::Pointer) {
        error(index_node.location, "operator[] does not return a reference");
        return {};
    }
    if (as_address) {
        // An lvalue result is an address typed as the element it names, the
        // same shape emit_lvalue produces for a member or an array element.
        return {call, function->type->return_type->element};
    }
    const auto ast_type = function_ast_types_.find(function);
    if (ast_type != function_ast_types_.end() && ast_type->second &&
        ast_type->second->return_type &&
        ast_type->second->return_type->is_reference &&
        function->type->return_type->kind == ir::TypeKind::Pointer) {
        return {builder_.create_load(function->type->return_type->element, call,
                                     false, 0, "subscript.load"),
                function->type->return_type->element};
    }
    return {call, function->type->return_type};
}
// Emits the begin()/end() form of a range-for. Returns false when the loop
// could not be built, so the caller can fall back to the array form.
bool CIRGen::emit_range_for_class(const frontend::AstNode& statement,
                                  ir::Function& begin_method,
                                  ir::Function& end_method,
                                  const std::string& value_name,
                                  bool bound_by_reference,
                                  ir::TypePtr element_ir,
                                  frontend::TypePtr element_type) {
    auto self = emit_lvalue(*statement.children[2]);
    if (!self.value) {
        return false;
    }
    const auto pointer_type = begin_method.type->return_type;
    auto* cursor = builder_.create_alloca(pointer_type, 0, "range.cursor");
    auto* limit = builder_.create_alloca(pointer_type, 0, "range.limit");
    const auto self_argument = [&]() -> ir::Value* {
        if (self.value->type == begin_method.type->parameters[0]) {
            return self.value;
        }
        return builder_.create(ir::Opcode::BitCast, begin_method.type->parameters[0],
                               {self.value}, "range.this");
    };
    builder_.create_store(
        create_call_instruction(&begin_method, {self_argument()}, pointer_type,
                                "range.begin"),
        cursor);
    builder_.create_store(
        create_call_instruction(&end_method, {self_argument()}, pointer_type,
                                "range.end"),
        limit);
    auto* value_address = builder_.create_alloca(
        bound_by_reference ? module_->types().pointer_type(element_ir) : element_ir, 0,
        value_name + ".range");
    declare_local(value_name,
                  LocalBinding{value_address,
                               bound_by_reference
                                   ? module_->types().pointer_type(element_ir)
                                   : element_ir,
                               element_type});
    auto* condition_block = current_function_->add_block("range.cond");
    auto* body_block = current_function_->add_block("range.body");
    auto* advance_block = current_function_->add_block("range.inc");
    auto* end_block = current_function_->add_block("range.end");
    emit_branch(condition_block);
    builder_.set_insert_point(condition_block);
    auto* current = builder_.create_load(pointer_type, cursor, false, 0, "range.at");
    auto* finished = builder_.create_load(pointer_type, limit, false, 0, "range.stop");
    auto* condition =
        builder_.create_icmp(ir::ICmpPredicate::Ne, current, finished, "range.cmp");
    emit_conditional_branch(condition, body_block, end_block);
    builder_.set_insert_point(body_block);
    if (bound_by_reference) {
        builder_.create_store(current, value_address);
    } else {
        builder_.create_store(
            builder_.create_load(element_ir, current, false, 0, "range.load"),
            value_address);
    }
    break_targets_.push_back({end_block, local_cleanups_.size()});
    continue_targets_.push_back({advance_block, local_cleanups_.size()});
    emit_statement(*statement.children[3]);
    break_targets_.pop_back();
    continue_targets_.pop_back();
    emit_branch(advance_block);
    builder_.set_insert_point(advance_block);
    auto* next = builder_.create_gep(
        element_ir, builder_.create_load(pointer_type, cursor, false, 0, "range.old"),
        {module_->constant_int(int_type(), 1)}, false, "range.next");
    builder_.create_store(next, cursor);
    emit_branch(condition_block);
    builder_.set_insert_point(end_block);
    return true;
}
CIRGen::Expression CIRGen::emit_subscript(const frontend::AstNode& expression) {
    if (expression.children.size() < 2 || !expression.children[0] ||
        !expression.children[1]) {
        return {};
    }
    if (expression.children[0]->kind == frontend::AstKind::Identifier) {
        if (auto* local = lookup_local(expression.children[0]->text)) {
            // A reference to a record names the record, so s[i] on a
            // reference parameter reaches operator[] like a direct object.
            const auto object_type = record_object_type(local->frontend_type);
            if (object_type && object_type->kind == frontend::TypeKind::Record &&
                object_type->record) {
                const auto methods = class_methods_.find(object_type->record->tag);
                if (methods != class_methods_.end()) {
                    const auto method = methods->second.find("operator[]");
                    if (method != methods->second.end()) {
                        auto self = emit_lvalue(*expression.children[0]);
                        auto index = emit_expression(*expression.children[1]);
                        if (self.value && index.value) {
                            auto* function = method->second;
                            if (self.value->type !=
                                function->type->parameters[0]) {
                                self.value = builder_.create(
                                    ir::Opcode::BitCast,
                                    function->type->parameters[0], {self.value},
                                    "subscript.this");
                            }
                            std::vector<ir::Value*> arguments{self.value};
                            if (index.value->type !=
                                    function->type->parameters[1]) {
                                index.value = promote(
                                    index.value, index.type,
                                    function->type->parameters[1]);
                            }
                            arguments.push_back(index.value);
                            auto* call = create_call_instruction(
                                function, std::move(arguments),
                                function->type->return_type, "subscript.call");
                            const auto ast_type =
                                function_ast_types_.find(function);
                            if (ast_type != function_ast_types_.end() &&
                                ast_type->second &&
                                ast_type->second->return_type &&
                                ast_type->second->return_type->is_reference &&
                                function->type->return_type->kind ==
                                    ir::TypeKind::Pointer) {
                                return {builder_.create_load(
                                            function->type->return_type->element,
                                            call, false, 0,
                                            "subscript.load"),
                                        function->type->return_type->element};
                            }
                            return {call, function->type->return_type};
                        }
                    }
                }
            }
        }
    }
    // (*this)[i] and (*p)[i] name the object the operand refers to, so the
    // record's operator[] receives that address rather than a loaded copy.
    if (expression.children[0]->kind == frontend::AstKind::UnaryExpression &&
        expression.children[0]->text == "*") {
        auto self = emit_lvalue(*expression.children[0]);
        if (self.value && self.type) {
            const auto record = ir_records_.find(self.type);
            if (record != ir_records_.end() && record->second) {
                const auto methods = class_methods_.find(record->second->tag);
                if (methods != class_methods_.end()) {
                    const auto method = methods->second.find("operator[]");
                    if (method != methods->second.end()) {
                        return subscript_operator(method->second, self,
                                                 *expression.children[1]);
                    }
                }
            }
        }
    }
    auto base = emit_expression(*expression.children[0]);
    auto index = emit_expression(*expression.children[1]);
    if (!base.value || !base.type || base.type->kind != ir::TypeKind::Pointer) {
        error(expression.location, "subscripted value is not a pointer");
        return {};
    }
    auto* address = builder_.create_gep(base.type->element, base.value, {index.value}, false,
                                        "element");
    address->type = base.type;
    const auto type = base.type->element;
    if (type && type->kind == ir::TypeKind::Array) {
        auto* zero = module_->constant_int(int_type(), 0);
        auto* decay = builder_.create_gep(type, address, {zero, zero}, false,
                                          "element.decay");
        decay->type = module_->types().pointer_type(type->element);
        return {decay, decay->type};
    }
    return {builder_.create_load(type, address, false, 0, "element.load"), type};
}

CIRGen::Expression CIRGen::emit_typeid_reference(const frontend::AstNode& expression) {
    frontend::TypePtr info = sema_.type_named("std::type_info");
    if (!info || info->kind != frontend::TypeKind::Record || !info->record) {
        info = sema_.type_named("type_info");
    }
    if (!info || info->kind != frontend::TypeKind::Record || !info->record) {
        error(expression.location, "typeid requires <typeinfo>");
        return {};
    }
    const auto info_type = map_type(info);
    if (!info_type) {
        return {};
    }
    if (expression.children.empty() || !expression.children.front()) {
        return {};
    }
    const auto& operand = *expression.children.front();
    const auto byte_type = module_->types().int_type(8);
    const auto static_reference = [&](frontend::TypePtr type) -> ir::Value* {
        if (!type) {
            error(expression.location, "typeid needs a type");
            return nullptr;
        }
        // A scalar has a descriptor too: one weak object per spelling, with an
        // empty base word, so 	ypeid(int) == typeid(int) holds and a class
        // descriptor never equals it.
        auto* global = type->kind == frontend::TypeKind::Record && type->record
                           ? ensure_typeinfo(*type->record)
                           : ensure_scalar_typeinfo(scalar_type_name(*type));
        return builder_.create(ir::Opcode::BitCast,
                               module_->types().pointer_type(info_type), {global},
                               "typeid.static");
    };
    ir::Value* address = nullptr;
    if (operand.kind == frontend::AstKind::TypeName) {
        address = static_reference(sema_.typeid_operand_type(expression));
    } else {
        const auto operand_type = sema_.typeid_operand_type(expression);
        const bool polymorphic = operand_type &&
                                 operand_type->kind == frontend::TypeKind::Record &&
                                 operand_type->record &&
                                 operand_type->record->polymorphic;
        if (!polymorphic) {
            address = static_reference(operand_type);
        } else {
            // The dynamic type is the one the object's vtable names, one word
            // before the first virtual slot. An lvalue expression carries the
            // object's address and its record type.
            auto object = emit_lvalue(operand);
            if (!object.value || !object.type ||
                object.type->kind != ir::TypeKind::Struct) {
                return {};
            }
            auto* zero = module_->constant_int(int_type(), 0);
            auto* vptr_slot = builder_.create_gep(object.type, object.value,
                                                  {zero, zero}, false, "typeid.vptr");
            vptr_slot->type = module_->types().pointer_type(
                module_->types().pointer_type(byte_type));
            auto* table = builder_.create_load(module_->types().pointer_type(byte_type),
                                               vptr_slot, false, 0, "typeid.vtable");
            // The descriptor sits one pointer below the first virtual entry,
            // and a pointer is not always four bytes (ADR 0037).
            const auto pointer_bytes = static_cast<std::int64_t>(
                target::get_target(module_->target()).pointer_bits / 8);
            auto* info_slot = builder_.create_gep(
                byte_type, table,
                {module_->constant_int(int_type(), -pointer_bytes)}, false,
                "typeid.slot");
            info_slot->type = module_->types().pointer_type(
                module_->types().pointer_type(byte_type));
            address = builder_.create_load(module_->types().pointer_type(byte_type),
                                           info_slot, false, 0, "typeid.info");
        }
    }
    if (!address) {
        return {};
    }
    const auto reference_type = module_->types().pointer_type(info_type);
    if (address->type != reference_type) {
        address = builder_.create(ir::Opcode::BitCast, reference_type, {address},
                                  "typeid.reference");
    }
    return {address, info_type};
}

CIRGen::Expression CIRGen::emit_typeid(const frontend::AstNode& expression) {
    auto reference = emit_typeid_reference(expression);
    if (!reference.value || !reference.type) {
        return {};
    }
    auto* value = builder_.create_load(reference.type, reference.value, false, 0,
                                       "typeid.value");
    return {value, reference.type};
}

void CIRGen::throw_object(const frontend::RecordInfo& record,
                          const frontend::SourceLocation& location) {
    const auto info = sema_.type_named(record.tag);
    if (!info || info->kind != frontend::TypeKind::Record) {
        error(location, "cannot throw '" + record.tag + "'");
        return;
    }
    const auto type = map_type(info);
    const auto void_type = module_->types().void_type();
    const auto byte_type = module_->types().int_type(8);
    const auto byte_pointer = module_->types().pointer_type(byte_type);
    auto* allocate = ensure_runtime_function("__cxa_allocate_exception", byte_pointer,
                                             {size_type()});
    auto* throw_function = ensure_runtime_function(
        "__cxa_throw", void_type, {byte_pointer, byte_pointer, byte_pointer});
    const auto size = ir_size_of(type);
    auto* memory = create_call_instruction(
        allocate, {module_->constant_int(size_type(), static_cast<std::int64_t>(size))},
        byte_pointer, "exception.memory");
    auto* object = builder_.create(ir::Opcode::BitCast,
                                   module_->types().pointer_type(type), {memory},
                                   "exception.object");
    if (auto* constructor = ensure_default_constructor(&record)) {
        create_call_instruction(constructor, {object}, nullptr, "exception.ctor");
    }
    // A declared constructor does not set the vptr itself; the code that
    // materializes the object does, as it does for locals and new.
    emit_vtable_init(object, type);
    auto* typeinfo = builder_.create(ir::Opcode::BitCast, byte_pointer,
                                     {ensure_typeinfo(record)}, "exception.type");
    ir::Value* destructor = module_->constant_null(byte_pointer);
    auto* cleanup = ensure_member_destructor(info->record.get());
    if (cleanup) {
        const auto slot_type = module_->types().int_type(profile_.pointer_bits);
        const auto table_type = module_->types().array_type(slot_type, 1);
        auto* table = module_->add_global(
            "__exception_dtor_" + std::to_string(static_local_counter_++),
            table_type, true, 2);
        table->initializer =
            module_->constant_address_array(table_type, {cleanup});
        auto* zero = module_->constant_int(int_type(), 0);
        auto* slot = builder_.create_gep(table_type, table, {zero, zero}, false,
                                         "dtor.slot");
        slot->type = module_->types().pointer_type(slot_type);
        auto* bits = builder_.create_load(slot_type, slot, false, 0, "dtor.value");
        destructor = builder_.create(ir::Opcode::IntToPtr, byte_pointer, {bits},
                                     "exception.dtor");
    }
    create_call_instruction(throw_function,
                            {memory, typeinfo, destructor},
                            void_type, "throw.exception");
    builder_.create_unreachable();
}

CIRGen::Expression CIRGen::emit_dynamic_cast(const frontend::AstNode& expression) {
    // Sema recorded both types; re-analysing here would not see the scopes the
    // operand was written in.
    const auto target_frontend = sema_.cast_target_type(expression);
    const auto source_frontend = sema_.cast_operand_type(expression);
    if (!target_frontend || !source_frontend) {
        return {};
    }
    const auto target = map_type(target_frontend);
    if (!target || target->kind != ir::TypeKind::Pointer) {
        return {};
    }
    // An lvalue or a reference operand names the object itself; its address is
    // what the runtime check takes. A pointer operand already is that address.
    const bool source_is_object =
        source_frontend->kind == frontend::TypeKind::Record ||
        source_frontend->is_reference;
    auto operand = source_is_object ? emit_lvalue(*expression.children[1])
                                    : emit_expression(*expression.children[1]);
    if (!operand.value) {
        return {};
    }
    const auto* source_record =
        source_frontend->kind == frontend::TypeKind::Record
            ? (source_frontend->record ? source_frontend->record.get() : nullptr)
            : (source_frontend->element && source_frontend->element->record
                   ? source_frontend->element->record.get()
                   : nullptr);
    // A oid* target names the complete object, which under single
    // inheritance is the operand's own address: no descriptor walk is needed.
    if (target_frontend->element &&
        target_frontend->element->kind == frontend::TypeKind::Void) {
        if (operand.type == target) {
            return {operand.value, target};
        }
        return {builder_.create(ir::Opcode::BitCast, target, {operand.value},
                                "cast.void"),
                target};
    }
    const auto* target_record = target_frontend->element &&
                                        target_frontend->element->record
                                    ? target_frontend->element->record.get()
                                    : nullptr;
    if (!source_record || !target_record) {
        return {};
    }
    // An upcast names a base class of the source, so no object can fail it.
    for (const auto* current = source_record; current != nullptr;
         current = current->base && current->base->record
                       ? current->base->record.get()
                       : nullptr) {
        if (current == target_record) {
            if (operand.type == target) {
                return {operand.value, target};
            }
            auto* cast = builder_.create(ir::Opcode::BitCast, target, {operand.value},
                                         "dynamic_cast.up");
            return {cast, target};
        }
    }
    // A downcast asks the runtime whether the object's dynamic type is or
    // derives from the target, and yields null when it does not.
    const auto byte_type = module_->types().int_type(8);
    const auto byte_pointer = module_->types().pointer_type(byte_type);
    const auto helper_type = module_->types().function_type(
        byte_pointer, {byte_pointer, byte_pointer}, false);
    auto* helper = module_->add_function("__krc_dynamic_cast", helper_type,
                                        ir::Linkage::External, true);
    if (helper->parameters.empty()) {
        helper->add_parameter(byte_pointer, "object");
        helper->add_parameter(byte_pointer, "target_type");
    }
    auto* object = operand.value->type == byte_pointer
                       ? operand.value
                       : builder_.create(ir::Opcode::BitCast, byte_pointer,
                                         {operand.value}, "dynamic_cast.object");
    auto* typeinfo_global = ensure_typeinfo(*target_record);
    auto* typeinfo = builder_.create(ir::Opcode::BitCast, byte_pointer,
                                     {typeinfo_global}, "dynamic_cast.type");
    auto* result = create_call_instruction(helper, {object, typeinfo}, byte_pointer,
                                        "dynamic_cast.call");
    if (target_frontend->is_reference) {
        // A reference cast cannot yield null: a failed check throws
        // std::bad_cast, which the caller's handlers see like any other throw.
        auto* failed = builder_.create_icmp(
            ir::ICmpPredicate::Eq, result, module_->constant_null(byte_pointer),
            "dynamic_cast.failed");
        auto* throw_block = current_function_->add_block("dynamic_cast.throw");
        auto* ok_block = current_function_->add_block("dynamic_cast.ok");
        emit_conditional_branch(failed, throw_block, ok_block);
        builder_.set_insert_point(throw_block);
        const auto info = sema_.type_named("std::bad_cast");
        if (!info || info->kind != frontend::TypeKind::Record || !info->record) {
            error(expression.location, "a reference dynamic_cast requires <typeinfo>");
            builder_.create_unreachable();
        } else {
            throw_object(*info->record, expression.location);
        }
        builder_.set_insert_point(ok_block);
    }
    auto* cast = builder_.create(ir::Opcode::BitCast, target, {result},
                                 "dynamic_cast.result");
    return {cast, target};
}

ir::Function* CIRGen::ensure_runtime_function(std::string_view name, ir::TypePtr result,
                                              const std::vector<ir::TypePtr>& parameters) {
    if (const auto found = functions_.find(std::string(name)); found != functions_.end()) {
        return found->second;
    }
    auto* function = module_->add_function(
        std::string(name), module_->types().function_type(result, parameters, false),
        ir::Linkage::External, true);
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        function->add_parameter(parameters[index], "arg" + std::to_string(index));
    }
    functions_[std::string(name)] = function;
    return function;
}

ir::Instruction* CIRGen::create_call_instruction(ir::Value* callee,
                                                 std::vector<ir::Value*> arguments,
                                                 ir::TypePtr result_type,
                                                 const std::string& name) {
    // A memory-class aggregate argument travels as a pointer to a caller-owned
    // copy, so the argument value is materialized and its address passed.
    if (const auto signature = call_signature(callee)) {
        const auto count = std::min(arguments.size(), signature->parameters.size());
        for (std::size_t index = 0; index < count; ++index) {
            auto* parameter = signature->parameters[index];
            auto* argument = arguments[index];
            if (!parameter || !argument || parameter->kind != ir::TypeKind::Pointer ||
                !parameter->element || parameter->element->kind != ir::TypeKind::Struct ||
                !argument->type || argument->type->kind != ir::TypeKind::Struct) {
                continue;
            }
            auto* copy = builder_.create_alloca(argument->type, 0, "argument.copy");
            // One whole-record load and store, which the back end moves as **one
            // word** -- four bytes on KR32 and two on KR16. A record larger than a
            // word therefore loses its tail here, and a three-byte record passed by
            // value is wrong on KR16 for exactly that reason; the copy wants the
            // argument's address and this path does not have it. Recorded in
            // implementation.md rather than changed underneath the layout work.
            builder_.create_store(argument, copy);
            arguments[index] =
                copy->type == parameter
                    ? static_cast<ir::Value*>(copy)
                    : builder_.create(ir::Opcode::BitCast, parameter, {copy},
                                      "argument.pointer");
        }
    }
    if (!owned_landing_pad(current_landing_pad_) || !current_function_ ||
        !builder_.insert_block()) {
        return builder_.create_call(callee, std::move(arguments), result_type, name);
    }
    auto* normal = current_function_->add_block("invoke.continue");
    auto* invoke = builder_.create_invoke(callee, std::move(arguments), result_type,
                                          normal, current_landing_pad_, name);
    builder_.set_insert_point(normal);
    return invoke;
}
void CIRGen::emit_throw(const frontend::AstNode& statement) {
    const auto void_type = module_->types().void_type();
    const auto byte_type = module_->types().int_type(8);
    const auto byte_pointer = module_->types().pointer_type(byte_type);
    if (statement.children.empty() || !statement.children.front()) {
        auto* rethrow = ensure_runtime_function("__cxa_rethrow", void_type, {});
        note_scope_cleanup();
        create_call_instruction(rethrow, {}, void_type, "rethrow");
        builder_.create_unreachable();
        return;
    }
    auto value = emit_expression(*statement.children.front());
    if (!value.value || !value.type || value.type->kind != ir::TypeKind::Struct) {
        error(statement.location, "throw requires a class type");
        return;
    }
    const auto record = ir_records_.find(value.type);
    if (record == ir_records_.end() || !record->second) {
        error(statement.location, "throw requires a class type");
        return;
    }
    auto* allocate = ensure_runtime_function("__cxa_allocate_exception", byte_pointer,
                                             {size_type()});
    auto* throw_function = ensure_runtime_function(
        "__cxa_throw", void_type, {byte_pointer, byte_pointer, byte_pointer});
    const auto size = ir_size_of(value.type);
    auto* object = create_call_instruction(
        allocate, {module_->constant_int(size_type(), static_cast<std::int64_t>(size))},
        byte_pointer, "exception.memory");
    auto* record_pointer = builder_.create(
        ir::Opcode::BitCast, module_->types().pointer_type(value.type), {object},
        "exception.object");
    builder_.create_store(value.value, record_pointer);
    auto* typeinfo = builder_.create(ir::Opcode::BitCast, byte_pointer,
                                     {ensure_typeinfo(*record->second)},
                                     "exception.type");
    // The throw unwinds through the objects that are live here, so its call site
    // needs a pad holding them (ADR 0027).
    note_scope_cleanup();
    ir::Value* destructor = module_->constant_null(byte_pointer);
    auto* cleanup = ensure_member_destructor(record->second);
    if (cleanup) {
        const auto slot_type = module_->types().int_type(profile_.pointer_bits);
        const auto table_type = module_->types().array_type(slot_type, 1);
        auto* table = module_->add_global(
            "__exception_dtor_" + std::to_string(static_local_counter_++),
            table_type, true, 2);
        table->initializer =
            module_->constant_address_array(table_type, {cleanup});
        auto* zero = module_->constant_int(int_type(), 0);
        auto* slot = builder_.create_gep(table_type, table, {zero, zero}, false,
                                         "dtor.slot");
        slot->type = module_->types().pointer_type(slot_type);
        auto* bits = builder_.create_load(slot_type, slot, false, 0, "dtor.value");
        destructor = builder_.create(ir::Opcode::IntToPtr, byte_pointer, {bits},
                                     "exception.dtor");
    }
    create_call_instruction(throw_function,
                            {object, typeinfo, destructor},
                            void_type, "throw");
    builder_.create_unreachable();
}

void CIRGen::emit_try(const frontend::AstNode& statement) {
    if (statement.children.empty() || !statement.children.front()) {
        return;
    }
    const auto void_type = module_->types().void_type();
    const auto byte_type = module_->types().int_type(8);
    const auto byte_pointer = module_->types().pointer_type(byte_type);
    auto* pad = current_function_->add_block("catch.pad");
    auto* end = current_function_->add_block("catch.end");
    const auto previous_pad = current_landing_pad_;
    current_landing_pad_ = pad;
    emit_statement(*statement.children.front());
    current_landing_pad_ = previous_pad;
    // The body may end in a throw, in which case the normal edge is dead.
    if (!builder_.insert_block()->terminated()) {
        builder_.create_br(end);
    }

    // The handler: the unwinder enters the pad with the exception object
    // pointer in R0 and its typeinfo in R1 (ADR 0026).
    builder_.set_insert_point(pad);
    auto* landing = builder_.create_landingpad("exception");
    auto* object_slot = builder_.create_gep(
        byte_type, landing, {module_->constant_int(int_type(), 0)}, false,
        "exception.object.slot");
    object_slot->type = module_->types().pointer_type(byte_pointer);
    auto* exception = builder_.create_load(byte_pointer, object_slot, false, 0,
                                           "exception.object");
    auto* type_slot = builder_.create_gep(
        byte_type, landing, {module_->constant_int(int_type(), 4)}, false,
        "exception.type.slot");
    type_slot->type = module_->types().pointer_type(byte_pointer);
    auto* exception_type =
        builder_.create_load(byte_pointer, type_slot, false, 0, "exception.type");
    auto* matches = ensure_runtime_function("__krc_eh_matches", int_type(),
                                            {byte_pointer, byte_pointer});
    auto* begin_catch = ensure_runtime_function("__cxa_begin_catch", byte_pointer,
                                                {byte_pointer});
    auto* end_catch = ensure_runtime_function("__cxa_end_catch", void_type, {});

    for (std::size_t index = 1; index < statement.children.size(); ++index) {
        const auto* clause = statement.children[index].get();
        if (!clause || clause->kind != frontend::AstKind::CatchClause) {
            continue;
        }
        const bool catch_all = clause->children.empty() ||
                               !clause->children.front() ||
                               clause->children.front()->kind !=
                                   frontend::AstKind::ParameterDeclaration;
        const frontend::AstNode* parameter =
            catch_all ? nullptr : clause->children.front().get();
        frontend::TypePtr frontend_type;
        if (parameter) {
            frontend_type = sema_.declaration_type(*parameter->children[0],
                                                   parameter->children[1].get());
        }
        const auto type = frontend_type ? map_type(frontend_type) : nullptr;
        if (!catch_all && (!type || !frontend_type || !frontend_type->is_reference ||
                           !frontend_type->element ||
                           !frontend_type->element->record)) {
            error(clause->location, "catch requires a reference to a class type");
            continue;
        }
        ir::Value* condition = module_->constant_int(module_->types().bool_type(), 1);
        if (!catch_all) {
            auto* catch_typeinfo = builder_.create(
                ir::Opcode::BitCast, byte_pointer,
                {ensure_typeinfo(*frontend_type->element->record)},
                "catch.type");
            auto* matched = create_call_instruction(
                matches, {exception_type, catch_typeinfo}, int_type(), "catch.matches");
            condition = builder_.create_icmp(ir::ICmpPredicate::Ne, matched,
                                             module_->constant_int(int_type(), 0),
                                             "catch.matched");
        }
        auto* handler = current_function_->add_block("catch.handler");
        auto* next = current_function_->add_block("catch.next");
        emit_conditional_branch(condition, handler, next);
        builder_.set_insert_point(handler);
        create_call_instruction(begin_catch, {exception}, byte_pointer,
                                "catch.begin");
        push_scope();
        if (parameter) {
            const auto name = parameter->children.size() > 1 && parameter->children[1]
                                  ? declarator_name(*parameter->children[1])
                                  : std::string{};
            if (!name.empty()) {
                // The handler sees a reference to the thrown object; the
                // exception object already holds the thrown value.
                auto* storage = builder_.create_alloca(type, 0, "catch.variable");
                auto* object = builder_.create(ir::Opcode::BitCast, type,
                                               {exception}, "catch.object");
                builder_.create_store(object, storage);
                declare_local(name, {storage, type, frontend_type});
            }
        }
        emit_statement(*clause->children.back());
        if (parameter) {
            pop_scope();
        }
        // A handler that ends in a throw or rethrow leaves the catch state to
        // the propagating exception; end_catch is not reached (ADR 0026).
        if (!builder_.insert_block()->terminated()) {
            create_call_instruction(end_catch, {}, void_type, "catch.end");
            builder_.create_br(end);
        }
        builder_.set_insert_point(next);
    }
    emit_resume(exception);
    builder_.set_insert_point(end);
}

CIRGen::Expression CIRGen::emit_cast(const frontend::AstNode& expression) {    if (expression.children.size() < 2 || !expression.children[0] ||
        !expression.children[1]) {
        return {};
    }
    if (!expression.labels.empty() && expression.labels.front() == "dynamic_cast") {
        return emit_dynamic_cast(expression);
    }
    const auto target_frontend =
        sema_.type_from_type_name_node(*expression.children[0]);
    const auto target = map_type(target_frontend);
    if (target_frontend && target_frontend->is_reference &&
        target_frontend->element) {
        auto address = emit_lvalue(*expression.children[1]);
        if (address.value) {
            // A reference to a record travels as its address, which is what a
            // record value is here. A reference to anything smaller is a value
            // in an expression: `int moved = static_cast<int&&>(first)` copies
            // the integer, and handing back the address put a pointer in it.
            const auto element = map_type(target_frontend->element);
            if (element && element->kind != ir::TypeKind::Struct &&
                element->kind != ir::TypeKind::Array) {
                return {builder_.create_load(element, address.value, false, 0,
                                             "reference.load"),
                        element};
            }
            return {address.value, target};
        }
        return {};
    }
    auto operand = emit_expression(*expression.children[1]);
    if (!operand.value || !target) {
        return {};
    }
    if (ir::is_void(target)) {
        return {};
    }
    if (ir::same_type(operand.type, target)) {
        return {operand.value, target};
    }
    if (operand.type->kind == ir::TypeKind::Struct &&
        (ir::is_integer(target) || ir::is_float(target))) {
        const char* conversion = nullptr;
        if (ir::is_integer(target)) {
            if (target->bits == 1) {
                conversion = "operator bool";
            } else if (target->bits == 8) {
                conversion = "operator char";
            } else if (target->bits == 16) {
                conversion = "operator short";
            } else if (target->bits == 32) {
                conversion = "operator int";
            } else if (target->bits == 64) {
                conversion = "operator long";
            }
        } else {
            conversion =
                target->bits == 64 ? "operator double" : "operator float";
        }
        if (conversion == nullptr) {
            // Other conversion-operator targets stay unsupported.
        } else {
        const auto record = ir_records_.find(operand.type);
        if (record != ir_records_.end() && record->second) {
            const auto methods = class_methods_.find(record->second->tag);
            if (methods != class_methods_.end()) {
                const auto method = methods->second.find(conversion);
                if (method != methods->second.end()) {
                    Expression self;
                    if (expression.children[1]->kind ==
                        frontend::AstKind::CallExpression) {
                        auto value =
                            emit_expression(*expression.children[1]);
                        if (value.value && value.type &&
                            value.type->kind == ir::TypeKind::Struct) {
                            auto* storage = builder_.create_alloca(
                                value.type, 0, "cast.temp");
                            builder_.create_store(value.value, storage);
                            self.value = storage;
                            self.type = value.type;
                        }
                    } else {
                        self = emit_lvalue(*expression.children[1]);
                    }
                    if (self.value) {
                        if (self.value->type !=
                            method->second->type->parameters[0]) {
                            self.value = builder_.create(
                                ir::Opcode::BitCast,
                                method->second->type->parameters[0],
                                {self.value}, "cast.this");
                        }
                        auto* call = create_call_instruction(
                            method->second, {self.value},
                            method->second->type->return_type, "cast.conversion");
                        return reference_result(call, method->second, method->second->type->return_type);


                    }
                }
            }
        }
        }
    }
    ir::Opcode opcode = ir::Opcode::BitCast;
    if (ir::is_integer(operand.type) && ir::is_integer(target)) {
        if (operand.type->bits == target->bits) {
            opcode = ir::Opcode::BitCast;
        } else if (operand.type->bits > target->bits) {
            opcode = ir::Opcode::Trunc;
        } else if (operand.type->is_signed) {
            opcode = ir::Opcode::SExt;
        } else {
            opcode = ir::Opcode::ZExt;
        }
    } else if (ir::is_float(operand.type) && ir::is_float(target)) {
        opcode = operand.type->bits > target->bits ? ir::Opcode::FPTrunc : ir::Opcode::FPExt;
    } else if (ir::is_float(operand.type) && ir::is_integer(target)) {
        opcode = target->is_signed ? ir::Opcode::FPToSI : ir::Opcode::FPToUI;
    } else if (ir::is_integer(operand.type) && ir::is_float(target)) {
        opcode = operand.type->is_signed ? ir::Opcode::SIToFP : ir::Opcode::UIToFP;
    } else if (ir::is_pointer(operand.type) && ir::is_integer(target)) {
        opcode = ir::Opcode::PtrToInt;
    } else if (ir::is_integer(operand.type) && ir::is_pointer(target)) {
        opcode = ir::Opcode::IntToPtr;
    }
    auto* value = builder_.create(opcode, target, {operand.value}, "cast");
    return {value, target};
}

CIRGen::Expression CIRGen::emit_sizeof(const frontend::AstNode& expression) {
    // sizeof...(pack) is the arity the instance folded to, not a type's size.
    if (expression.text == "sizeof...") {
        long long folded = 0;
        if (!sema_.folded_constant(expression, folded)) {
            error(expression.location,
                  "the arity of this parameter pack is not known here");
            return {};
        }
        const auto type = size_type();
        return {module_->constant_int(type, folded), type};
    }

    if (expression.children.empty() || !expression.children.front()) {
        return {};
    }
    const auto* operand = expression.children.front().get();
    std::uint64_t result = 0;
    if (operand->kind == frontend::AstKind::TypeName) {
        const auto frontend_type = sema_.type_from_type_name_node(*operand);
        result = expression.kind == frontend::AstKind::SizeofExpression
                     ? sema_.size_of_type(frontend_type)
                     : sema_.align_of_type(frontend_type);
    } else if (operand->kind == frontend::AstKind::Identifier) {
        if (auto* local = lookup_local(operand->text); local && local->frontend_type) {
            result = expression.kind == frontend::AstKind::SizeofExpression
                         ? sema_.size_of_type(local->frontend_type)
                         : sema_.align_of_type(local->frontend_type);
        } else if (const auto global = globals_.find(operand->text);
                   global != globals_.end()) {
            result = expression.kind == frontend::AstKind::SizeofExpression
                         ? ir_size_of(global->second->element_type)
                         : 1;
        } else {
            const auto value = emit_expression(*operand);
            result = expression.kind == frontend::AstKind::SizeofExpression
                         ? ir_size_of(value.type)
                         : 1;
        }
    } else if (operand->kind == frontend::AstKind::CharLiteral) {
        // A character literal is emitted as an i32 constant and truncated only where it is
        // stored, so measuring the emitted value made `sizeof('a')` four while the same
        // expression folded to one in Sema -- a compile-time `static_assert` passed and the
        // run-time check failed in the same build. Re-analysis is safe for a literal: its
        // type depends on the token and the language mode, not on template bindings.
        const auto literal_type = sema_.expression_type(*operand);
        result = expression.kind == frontend::AstKind::SizeofExpression
                     ? sema_.size_of_type(literal_type)
                     : sema_.align_of_type(literal_type);
    } else if (const auto analysed = sema_.cached_expression_type(*operand);
               analysed && analysed->kind != frontend::TypeKind::Function) {
        // The same authority for anything whose type analysis was recorded.
        result = expression.kind == frontend::AstKind::SizeofExpression
                     ? sema_.size_of_type(analysed)
                     : sema_.align_of_type(analysed);
    } else {
        const auto value = emit_expression(*operand);
        result = expression.kind == frontend::AstKind::SizeofExpression
                     ? ir_size_of(value.type)
                     : 1;
    }
    const auto result_type = size_type();
    return {module_->constant_int(result_type, static_cast<std::int64_t>(result)), result_type};
}

std::uint64_t CIRGen::ir_size_of(ir::TypePtr type) const {
    if (!type) {
        return 0;
    }
    switch (type->kind) {
    case ir::TypeKind::Void:
        return 1;
    case ir::TypeKind::Integer:
    case ir::TypeKind::Float:
        return type->bits == 0 ? 1 : type->bits / 8;
    case ir::TypeKind::Pointer:
        return profile_.pointer_bits / 8;
    case ir::TypeKind::Array:
        return ir_size_of(type->element) * type->count;
    case ir::TypeKind::Struct: {
        // A field is aligned by its own alignment, not by a fixed four bytes. The
        // fixed value is a KR32-shaped guess: it made `struct { char a, b, c; }`
        // twelve bytes when Sema lays it out as three, so `sizeof(<expression>)`
        // disagreed with `sizeof(<type>)` and `is_memory_class_argument` classified
        // a small aggregate as memory class (ADR 0046).
        std::uint64_t size = 0;
        std::uint64_t alignment = 1;
        for (const auto& field : type->fields) {
            const auto field_alignment = ir_align_of(field);
            alignment = std::max(alignment, field_alignment);
            size = (size + field_alignment - 1) / field_alignment * field_alignment;
            size += ir_size_of(field);
        }
        return (size + alignment - 1) / alignment * alignment;
    }
    case ir::TypeKind::Union: {
        std::uint64_t size = 0;
        for (const auto& field : type->fields) {
            size = std::max(size, ir_size_of(field));
        }
        return size;
    }
    case ir::TypeKind::Function:
        return 0;
    }
    return 0;
}

std::uint64_t CIRGen::ir_align_of(ir::TypePtr type) const {
    if (!type) {
        return 1;
    }
    switch (type->kind) {
    case ir::TypeKind::Integer:
    case ir::TypeKind::Float:
        return std::max<std::uint64_t>(1, (type->bits + 7) / 8);
    case ir::TypeKind::Pointer:
        return profile_.pointer_bits / 8;
    case ir::TypeKind::Array:
        return ir_align_of(type->element);
    case ir::TypeKind::Struct:
    case ir::TypeKind::Union: {
        std::uint64_t alignment = 1;
        for (const auto& field : type->fields) {
            alignment = std::max(alignment, ir_align_of(field));
        }
        return alignment;
    }
    case ir::TypeKind::Void:
    case ir::TypeKind::Function:
        return 1;
    }
    return 1;
}

ir::Value* CIRGen::promote(ir::Value* value, ir::TypePtr from, ir::TypePtr to) {
    if (!value || !from || !to || ir::same_type(from, to)) {
        return value;
    }
    if (ir::is_integer(from) && ir::is_integer(to)) {
        if (from->bits == to->bits) {
            return from->is_signed == to->is_signed
                       ? value
                       : builder_.create(ir::Opcode::BitCast, to, {value}, "retype");
        }
        if (from->bits > to->bits) {
            return builder_.create(ir::Opcode::Trunc, to, {value}, "trunc");
        }
        return builder_.create(from->is_signed ? ir::Opcode::SExt : ir::Opcode::ZExt, to,
                               {value}, "extend");
    }
    if (ir::is_float(from) && ir::is_float(to)) {
        return builder_.create(from->bits > to->bits ? ir::Opcode::FPTrunc
                                                     : ir::Opcode::FPExt,
                               to, {value}, "fpcast");
    }
    if (ir::is_float(from) && ir::is_integer(to)) {
        return builder_.create(to->is_signed ? ir::Opcode::FPToSI : ir::Opcode::FPToUI,
                               to, {value}, "fptoint");
    }
    if (ir::is_integer(from) && ir::is_float(to)) {
        return builder_.create(from->is_signed ? ir::Opcode::SIToFP : ir::Opcode::UIToFP,
                               to, {value}, "inttofp");
    }
    if (ir::is_pointer(from) && ir::is_pointer(to)) {
        return builder_.create(ir::Opcode::BitCast, to, {value}, "bitcast");
    }
    if (value->kind == ir::ValueKind::Function && ir::is_pointer(to)) {
        return builder_.create(ir::Opcode::BitCast, to, {value}, "function.ptr");
    }
    if (ir::is_integer(from) && ir::is_pointer(to)) {
        if (const auto* integer = dynamic_cast<const ir::ConstantInt*>(value);
            integer && integer->value == 0) {
            return module_->constant_null(to);
        }
        return builder_.create(ir::Opcode::IntToPtr, to, {value}, "inttoptr");
    }
    if (ir::is_pointer(from) && ir::is_integer(to)) {
        return builder_.create(ir::Opcode::PtrToInt, to, {value}, "ptrttoint");
    }
    return value;
}

ir::Value* CIRGen::constant_int(ir::TypePtr type, std::int64_t value) {
    return module_->constant_int(type, value);
}

}  // namespace krc::irgen


