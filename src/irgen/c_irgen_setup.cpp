#include "irgen/c_irgen_internal.h"

namespace krc::irgen {

CIRGen::CIRGen(frontend::Sema& sema, const frontend::AstNode& translation_unit,
               const target::TargetProfile& profile, frontend::DiagnosticEngine& diagnostics)
    : sema_(sema), unit_(translation_unit), profile_(profile), diagnostics_(diagnostics),
      module_(std::make_unique<ir::Module>(profile.id)), builder_(module_->types()) {}

bool CIRGen::is_referenced(ir::Function* target) const {
    if (target == nullptr) {
        return false;
    }
    for (const auto& function : module_->functions()) {
        if (!function || function.get() == target) {
            continue;
        }
        for (const auto& block : function->blocks) {
            if (!block) {
                continue;
            }
            for (const auto& instruction : block->instructions) {
                if (!instruction) {
                    continue;
                }
                for (const auto* operand : instruction->operands) {
                    if (operand == target) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

std::unique_ptr<ir::Module> CIRGen::generate() {
    collect_declarations();
    clone_inherited_methods();
    for (const auto& [function, definition] : method_bodies_) {
        // Constructors and destructors stay eager: an object's lifetime is
        // reached through cleanup and member-destructor helpers that are
        // themselves created while other bodies are being built, so a later
        // reference scan cannot see them. Everything else waits for a caller.
        const bool lifecycle = function->name.find("_ctor_") != std::string::npos ||
                               function->name.find("_dtor") != std::string::npos;
        if (instance_members_.count(function) != 0 && !lifecycle) {
            // Left as a declaration until something refers to it.
            function->declaration = true;
            pending_bodies_.emplace(function, definition);
            continue;
        }
        generate_function(*definition, function);
    }
    for (const auto* definition : definitions_) {
        if (definition) {
            // A pattern whose body depends on a value parameter or on a pack is a
            // signature: it has no body Sema could analyse, so the instantiation is
            // the only definition (ADR 0031, ADR 0045). Sema decides this question,
            // and code generation asks it of the same definition -- the two used to
            // hold separate copies of the test, and a pack declared in the template
            // parameter list fell between them.
            if (sema_.is_pattern_signature(*definition)) {
                const auto name = declarator_name(*definition->children[1]);
                const auto found = functions_.find(name);
                if (found != functions_.end() && found->second) {
                    found->second->declaration = true;
                }
                continue;
            }
            generate_function(*definition);
        }
    }
    for (const auto& [type, record] : ir_records_) {
        static_cast<void>(type);
        static_cast<void>(record);
    }
    // A pending body is built once something refers to it, and its own calls may
    // pull in more, so the loop runs to a fixed point. A body nothing reaches
    // stays a declaration (ADR 0039).
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& entry : pending_bodies_) {
            if (!entry.second || !entry.first ||
                generated_pending_.count(entry.first) != 0 ||
                !is_referenced(entry.first)) {
                continue;
            }
            generated_pending_.insert(entry.first);
            entry.first->declaration = false;
            generate_function(*entry.second, entry.first);
            changed = true;
        }
    }
    generate_global_constructors();
    finalize_vtables();
    sema_.break_cycles();
    function_ast_types_.clear();
    return std::move(module_);
}

ir::Function* CIRGen::ensure_default_constructor(
    const frontend::RecordInfo* record) {
    if (!record) {
        return nullptr;
    }
    if (auto* existing = select_constructor(record->tag, 0)) {
        return existing;
    }
    const auto cached = default_constructors_.find(record);
    if (cached != default_constructors_.end()) {
        return cached->second;
    }
    const auto type_entry = record_types_.find(record);
    if (type_entry == record_types_.end() || !type_entry->second) {
        return nullptr;
    }
    ir::Function* base_constructor = nullptr;
    if (record->base && record->base->kind == frontend::TypeKind::Record &&
        record->base->record) {
        base_constructor = ensure_default_constructor(record->base->record.get());
    }
    bool needs_construction = record->polymorphic || base_constructor != nullptr;
    for (const auto& member : record->members) {
        if (member.type && member.type->kind == frontend::TypeKind::Record &&
            member.type->record &&
            ensure_default_constructor(member.type->record.get()) != nullptr) {
            needs_construction = true;
        }
        if (member.type && member.type->kind == frontend::TypeKind::Array &&
            member.type->element && member.type->element->record &&
            ensure_default_constructor(member.type->element->record.get()) !=
                nullptr) {
            needs_construction = true;
        }
        if (member.default_initializer) {
            needs_construction = true;
        }
    }
    if (!needs_construction) {
        default_constructors_[record] = nullptr;
        return nullptr;
    }
    const auto type = type_entry->second;
    auto* function_type = module_->types().function_type(
        module_->types().void_type(),
        {module_->types().pointer_type(type)}, false);
    auto* function = module_->add_function(record->tag + "_ctor_0",
                                           function_type, ir::Linkage::Weak,
                                           false);
    function->debug_name = record->tag + "::" + record->tag;
    function->add_parameter(module_->types().pointer_type(type), "this");
    const auto previous_function = current_function_;
    const auto previous_class = current_class_;
    const auto previous_destructor = in_destructor_;
    const auto previous_constructor = in_constructor_;
    auto* previous_block = builder_.insert_block();
    current_function_ = function;
    current_class_ = record;
    in_destructor_ = false;
    in_constructor_ = true;
    auto* entry = function->add_block("entry");
    builder_.reset_value_names();
    builder_.set_insert_point(entry);
    auto* self = static_cast<ir::Value*>(function->parameters[0].get());
    if (base_constructor) {
        auto* base_self = self;
        if (base_self->type != base_constructor->type->parameters[0]) {
            base_self = builder_.create(
                ir::Opcode::BitCast, base_constructor->type->parameters[0],
                {base_self}, "implicit.base.this");
        }
        create_call_instruction(base_constructor, {base_self}, nullptr,
                             "implicit.base.ctor");
    }
    std::size_t field_position = 0;
    if (record->base && record->base->kind == frontend::TypeKind::Record &&
        record->base->record) {
        const auto base_type = record_types_.find(record->base->record.get());
        if (base_type != record_types_.end()) {
            field_position = base_type->second->fields.size();
        }
    } else if (record->polymorphic) {
        field_position = 1;
    }
    for (const auto& member : record->members) {
        auto* constructor =
            member.type && member.type->kind == frontend::TypeKind::Record &&
                    member.type->record
                ? ensure_default_constructor(member.type->record.get())
                : nullptr;
        if (field_position >= type->fields.size()) {
            break;
        }
        auto* zero = module_->constant_int(int_type(), 0);
        auto* field_index = module_->constant_int(
            int_type(), static_cast<std::int64_t>(field_position));
        auto* field = builder_.create_gep(type, self, {zero, field_index}, false,
                                          "implicit.field");
        field->type = module_->types().pointer_type(
            type->fields[field_position]);
        if (member.default_initializer &&
            !member.default_initializer->children.empty() &&
            member.default_initializer->children.front()) {
            emit_initializer(*member.default_initializer->children.front(), field,
                             type->fields[field_position]);
        } else if (member.type &&
                   member.type->kind == frontend::TypeKind::Array &&
                   member.type->element && member.type->element->record) {
            auto* element_constructor = ensure_default_constructor(
                member.type->element->record.get());
            if (element_constructor) {
                for (std::uint64_t element_position = 0;
                     element_position < type->fields[field_position]->count;
                     ++element_position) {
                    auto* element_index = module_->constant_int(
                        int_type(),
                        static_cast<std::int64_t>(element_position));
                    auto* element = builder_.create_gep(
                        type->fields[field_position], field,
                        {zero, element_index}, false,
                        "implicit.member.element");
                    element->type = module_->types().pointer_type(
                        type->fields[field_position]->element);
                    auto* member_self = element;
                    if (member_self->type !=
                        element_constructor->type->parameters[0]) {
                        member_self = builder_.create(
                            ir::Opcode::BitCast,
                            element_constructor->type->parameters[0],
                            {member_self}, "implicit.member.this");
                    }
                    create_call_instruction(element_constructor, {member_self},
                                         nullptr, "implicit.member.ctor");
                }
            }
        } else if (constructor) {
            auto* member_self = field;
            if (member_self->type != constructor->type->parameters[0]) {
                member_self = builder_.create(
                    ir::Opcode::BitCast, constructor->type->parameters[0],
                    {member_self}, "implicit.member.this");
            }
            create_call_instruction(constructor, {member_self}, nullptr,
                                 "implicit.member.ctor");
        }
        field_position++;
    }
    if (record->polymorphic) {
        emit_vtable_init(self, type);
    }
    builder_.create_ret(nullptr);
    current_function_ = previous_function;
    current_class_ = previous_class;
    in_destructor_ = previous_destructor;
    in_constructor_ = previous_constructor;
    builder_.set_insert_point(previous_block);
    default_constructors_[record] = function;
    return function;
}

ir::Function* CIRGen::ensure_member_destructor(
    const frontend::RecordInfo* record) {
    if (!record) {
        return nullptr;
    }
    const auto cached = member_destructors_.find(record);
    if (cached != member_destructors_.end()) {
        return cached->second;
    }
    auto record_type = record_types_.find(record);
    if (record_type == record_types_.end()) {
        // The record may not have been mapped yet: a template argument names a
        // type without mapping its record, so a class template that owns one
        // and deletes it found nothing here and emitted only the free. Mapping
        // it now is what makes `unique_ptr<T>` call T's destructor.
        static_cast<void>(map_record(*record));
        record_type = record_types_.find(record);
    }
    if (record_type == record_types_.end()) {
        return nullptr;
    }
    struct MemberDestructor {
        std::size_t position = 0;
        ir::Function* function = nullptr;
        std::uint64_t count = 1;
    };
    std::vector<MemberDestructor> member_destructors;
    for (std::size_t member_index = 0;
         member_index < record->members.size(); ++member_index) {
        const auto& member = record->members[member_index];
        const frontend::TypePtr record_type_of_member =
            member.type && member.type->kind == frontend::TypeKind::Array
                ? member.type->element
                : member.type;
        if (!record_type_of_member ||
            record_type_of_member->kind != frontend::TypeKind::Record ||
            !record_type_of_member->record) {
            continue;
        }
        const auto methods =
            class_methods_.find(record_type_of_member->record->tag);
        if (methods == class_methods_.end()) {
            continue;
        }
        const auto found =
            methods->second.find("~" + record_type_of_member->record->tag);
        if (found != methods->second.end()) {
            const std::uint64_t count =
                member.type->kind == frontend::TypeKind::Array
                    ? member.type->array_count
                    : 1;
            member_destructors.push_back({member_index, found->second, count});
        }
    }
    ir::Function* base_destructor = nullptr;
    if (record->base &&
        record->base->kind == frontend::TypeKind::Record &&
        record->base->record) {
        const auto base_methods =
            class_methods_.find(record->base->record->tag);
        if (base_methods != class_methods_.end()) {
            const auto found = base_methods->second.find(
                "~" + record->base->record->tag);
            if (found != base_methods->second.end() &&
                !found->second->declaration &&
                !found->second->blocks.empty()) {
                base_destructor = found->second;
            }
        }
        if (!base_destructor) {
            base_destructor =
                ensure_member_destructor(record->base->record.get());
        }
    }
    ir::Function* own_destructor = nullptr;
    const auto own_methods = class_methods_.find(record->tag);
    if (own_methods != class_methods_.end()) {
        const auto found = own_methods->second.find("~" + record->tag);
        // A destructor whose body has not been generated yet is still the one to
        // call: the call is emitted now and the body follows when its method is
        // generated. Requiring a non-empty body here made a class template's
        // `delete` free its object without destroying it whenever the owned
        // type's destructor came later in the translation unit.
        if (found != own_methods->second.end() &&
            !found->second->declaration) {
            own_destructor = found->second;
        }
    }
    if (member_destructors.empty() && !base_destructor &&
        !own_destructor) {
        return nullptr;
    }
    auto* function_type = module_->types().function_type(
        module_->types().void_type(),
        {module_->types().pointer_type(record_type->second)}, false);
    auto* function = module_->add_function(
        "__krc_member_dtor_" + record->tag, function_type,
        ir::Linkage::Internal, false);
    function->debug_name = record->tag + "::~" + record->tag;
    function->add_parameter(module_->types().pointer_type(record_type->second),
                            "this");
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
    const auto type = record_type->second;
    auto* this_argument = function->parameters[0].get();
    if (own_destructor) {
        auto* self = static_cast<ir::Value*>(this_argument);
        if (self->type != own_destructor->type->parameters[0]) {
            self = builder_.create(
                ir::Opcode::BitCast,
                own_destructor->type->parameters[0], {self},
                "own.dtor.this");
        }
        create_call_instruction(own_destructor, {self}, nullptr, "own.dtor");
    }
    const auto field_offset =
        type->fields.size() - record->members.size();
    for (auto entry_it = member_destructors.rbegin();
         entry_it != member_destructors.rend(); ++entry_it) {
        const auto position = field_offset + entry_it->position;
        if (position >= type->fields.size()) {
            continue;
        }
        auto* zero = module_->constant_int(int_type(), 0);
        auto* field_index = module_->constant_int(
            int_type(), static_cast<std::int64_t>(position));
        auto* field = builder_.create_gep(
            type, this_argument, {zero, field_index}, false,
            "member.dtor.field");
        field->type = module_->types().pointer_type(type->fields[position]);
        const auto call_member_destructor =
            [&](ir::Value* address) {
                auto* self = address;
                if (self->type !=
                    entry_it->function->type->parameters[0]) {
                    self = builder_.create(
                        ir::Opcode::BitCast,
                        entry_it->function->type->parameters[0], {self},
                        "member.dtor.this");
                }
                create_call_instruction(entry_it->function, {self}, nullptr,
                                     "member.dtor");
            };
        if (entry_it->count <= 1) {
            call_member_destructor(field);
            continue;
        }
        auto* index_slot =
            builder_.create_alloca(int_type(), 0, "member.dtor.index");
        builder_.create_store(module_->constant_int(int_type(), 0),
                              index_slot);
        auto* condition = current_function_->add_block("member.dtor.cond");
        auto* body = current_function_->add_block("member.dtor.body");
        auto* done = current_function_->add_block("member.dtor.done");
        emit_branch(condition);
        builder_.set_insert_point(condition);
        auto* index = builder_.create_load(int_type(), index_slot, false, 0,
                                           "member.dtor.index.load");
        auto* limit = module_->constant_int(
            int_type(), static_cast<std::int64_t>(entry_it->count));
        auto* compare = builder_.create_icmp(ir::ICmpPredicate::Ult, index,
                                             limit, "member.dtor.cmp");
        emit_conditional_branch(compare, body, done);
        builder_.set_insert_point(body);
        auto* element = builder_.create_gep(
            type->fields[position], field, {zero, index}, false,
            "member.dtor.element");
        element->type = module_->types().pointer_type(
            type->fields[position]->element);
        call_member_destructor(element);
        auto* next = builder_.create_add(
            index, module_->constant_int(int_type(), 1), "member.dtor.next");
        builder_.create_store(next, index_slot);
        emit_branch(condition);
        builder_.set_insert_point(done);
    }
    if (base_destructor) {
        auto* self = static_cast<ir::Value*>(this_argument);
        if (self->type != base_destructor->type->parameters[0]) {
            self = builder_.create(
                ir::Opcode::BitCast,
                base_destructor->type->parameters[0], {self},
                "base.dtor.this");
        }
        create_call_instruction(base_destructor, {self}, nullptr,
                             "base.dtor");
    }
    builder_.create_ret(nullptr);
    scopes_ = std::move(previous_scopes);
    current_function_ = previous_function;
    current_class_ = previous_class;
    builder_.set_insert_point(previous_block);
    member_destructors_.emplace(record, function);
    return function;
}

void CIRGen::generate_global_constructors() {
    if (global_ctors_.empty() && global_initializers_.empty() &&
        local_static_dtors_.empty()) {
        return;
    }
    const auto void_type = module_->types().void_type();
    const auto void_function =
        module_->types().function_type(void_type, {}, false);
    std::vector<ir::Function*> constructors;
    std::size_t constructor_index = 0;
    for (const auto& info : global_ctors_) {
        if (!info.global || !info.record) {
            continue;
        }
        auto* constructor =
            select_constructor(info.record->tag, info.arguments.size());
        if (!constructor && info.default_construct) {
            auto* function = module_->add_function(
                "__krc_global_ctor_" + std::to_string(constructor_index++),
                void_function, ir::Linkage::Internal, false);
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
            const auto type = info.global->element_type;
            if (info.record->base &&
                info.record->base->kind == frontend::TypeKind::Record &&
                info.record->base->record) {
                auto* base_constructor = select_constructor(
                    info.record->base->record->tag, 0);
                if (base_constructor) {
                    auto* self = static_cast<ir::Value*>(info.global);
                    if (self->type !=
                        base_constructor->type->parameters[0]) {
                        self = builder_.create(
                            ir::Opcode::BitCast,
                            base_constructor->type->parameters[0], {self},
                            "global.base.ctor.this");
                    }
                    create_call_instruction(base_constructor, {self}, nullptr,
                                         "global.base.ctor");
                }
            }
            const auto field_offset =
                type->fields.size() - info.record->members.size();
            for (std::size_t member_index = 0;
                 member_index < info.record->members.size() &&
                 member_index < type->fields.size();
                 ++member_index) {
                const auto& member = info.record->members[member_index];
                const auto position = field_offset + member_index;
                if (position >= type->fields.size()) {
                    continue;
                }
                auto* zero = module_->constant_int(int_type(), 0);
                auto* field_index = module_->constant_int(
                    int_type(), static_cast<std::int64_t>(position));
                auto* field = builder_.create_gep(
                    type, info.global, {zero, field_index}, false,
                    "global.default.field");
                field->type = module_->types().pointer_type(
                    type->fields[position]);
                if (member.default_initializer &&
                    !member.default_initializer->children.empty() &&
                    member.default_initializer->children.front()) {
                    emit_initializer(
                        *member.default_initializer->children.front(), field,
                        type->fields[position]);
                    continue;
                }
                if (member.type &&
                    member.type->kind == frontend::TypeKind::Array &&
                    member.type->element && member.type->element->record) {
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
                                auto* element_index = module_->constant_int(
                                    int_type(),
                                    static_cast<std::int64_t>(
                                        element_position));
                                auto* element = builder_.create_gep(
                                    type->fields[position], field,
                                    {zero, element_index}, false,
                                    "global.member.ctor.element");
                                element->type =
                                    module_->types().pointer_type(
                                        type->fields[position]->element);
                                auto* self = element;
                                if (self->type !=
                                    element_constructor->type
                                        ->parameters[0]) {
                                    self = builder_.create(
                                        ir::Opcode::BitCast,
                                        element_constructor->type
                                            ->parameters[0],
                                        {self}, "global.member.ctor.this");
                                }
                                create_call_instruction(
                                    element_constructor, {self}, nullptr,
                                    "global.member.ctor");
                            }
                        }
                    }
                    continue;
                }
                if (!member.type ||
                    member.type->kind != frontend::TypeKind::Record ||
                    !member.type->record) {
                    continue;
                }
                auto* member_constructor = select_constructor(
                    member.type->record->tag, 0);
                if (!member_constructor) {
                    continue;
                }
                auto* self = field;
                if (self->type !=
                    member_constructor->type->parameters[0]) {
                    self = builder_.create(
                        ir::Opcode::BitCast,
                        member_constructor->type->parameters[0], {self},
                        "global.member.this");
                }
                create_call_instruction(member_constructor, {self}, nullptr,
                                     "global.member.ctor");
            }
            builder_.create_ret(nullptr);
            scopes_ = std::move(previous_scopes);
            current_function_ = previous_function;
            current_class_ = previous_class;
            builder_.set_insert_point(previous_block);
            constructors.push_back(function);
            continue;
        }
        if (!constructor) {
            continue;
        }
        auto* function = module_->add_function(
            "__krc_global_ctor_" + std::to_string(constructor_index++),
            void_function, ir::Linkage::Internal, false);
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
        auto* self = static_cast<ir::Value*>(info.global);
        if (self->type != constructor->type->parameters[0]) {
            self = builder_.create(ir::Opcode::BitCast,
                                   constructor->type->parameters[0], {self},
                                   "global.this");
        }
        std::vector<ir::Value*> arguments{self};
        for (std::size_t index = 0; index < info.arguments.size(); ++index) {
            if (!info.arguments[index]) {
                continue;
            }
            auto argument = emit_expression(*info.arguments[index]);
            if (!argument.value) {
                continue;
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
                             "global.ctor");
        builder_.create_ret(nullptr);
        scopes_ = std::move(previous_scopes);
        current_function_ = previous_function;
        current_class_ = previous_class;
        builder_.set_insert_point(previous_block);
        constructors.push_back(function);
    }
    std::vector<ir::Function*> destructors;
    std::size_t destructor_index = 0;
    for (const auto& info : global_ctors_) {
        if (!info.global || !info.record) {
            continue;
        }
        ir::Function* destructor = nullptr;
        for (auto* current = info.record; current != nullptr;
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
        ir::Function* base_destructor = nullptr;
        if (info.record->base &&
            info.record->base->kind == frontend::TypeKind::Record &&
            info.record->base->record) {
            const auto base_methods =
                class_methods_.find(info.record->base->record->tag);
            if (base_methods != class_methods_.end()) {
                const auto found = base_methods->second.find(
                    "~" + info.record->base->record->tag);
                if (found != base_methods->second.end()) {
                    base_destructor = found->second;
                }
            }
            if (!base_destructor) {
                base_destructor = ensure_member_destructor(
                    info.record->base->record.get());
            }
        }
        if (!destructor) {
            bool has_member_destructor = false;
            for (const auto& member : info.record->members) {
                if (member.type &&
                    member.type->kind == frontend::TypeKind::Array &&
                    member.type->element && member.type->element->record) {
                    const auto element_methods = class_methods_.find(
                        member.type->element->record->tag);
                    if (element_methods != class_methods_.end() &&
                        element_methods->second.find(
                            "~" + member.type->element->record->tag) !=
                            element_methods->second.end()) {
                        has_member_destructor = true;
                    }
                    continue;
                }
                if (!member.type ||
                    member.type->kind != frontend::TypeKind::Record ||
                    !member.type->record) {
                    continue;
                }
                const auto member_methods =
                    class_methods_.find(member.type->record->tag);
                if (member_methods != class_methods_.end() &&
                    member_methods->second.find(
                        "~" + member.type->record->tag) !=
                        member_methods->second.end()) {
                    has_member_destructor = true;
                }
            }
            if (!has_member_destructor && !base_destructor) {
                continue;
            }
        }
        auto* function = module_->add_function(
            "__krc_global_dtor_" + std::to_string(destructor_index++),
            void_function, ir::Linkage::Internal, false);
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
        auto* self = static_cast<ir::Value*>(info.global);
        if (destructor) {
            if (self->type != destructor->type->parameters[0]) {
                self = builder_.create(ir::Opcode::BitCast,
                                       destructor->type->parameters[0],
                                       {self}, "global.dtor.this");
            }
            create_call_instruction(destructor, {self}, nullptr, "global.dtor");
        }
        const auto type = info.global->element_type;
        const auto field_offset =
            type->fields.size() - info.record->members.size();
        for (std::size_t member_index = info.record->members.size();
             member_index > 0; --member_index) {
            const auto& member = info.record->members[member_index - 1];
            if (member.type &&
                member.type->kind == frontend::TypeKind::Array &&
                member.type->element && member.type->element->record) {
                const auto position = field_offset + member_index - 1;
                if (position >= type->fields.size()) {
                    continue;
                }
                const auto element_methods = class_methods_.find(
                    member.type->element->record->tag);
                if (element_methods == class_methods_.end()) {
                    continue;
                }
                const auto found = element_methods->second.find(
                    "~" + member.type->element->record->tag);
                if (found == element_methods->second.end()) {
                    continue;
                }
                auto* zero = module_->constant_int(int_type(), 0);
                auto* field_index = module_->constant_int(
                    int_type(), static_cast<std::int64_t>(position));
                auto* field = builder_.create_gep(
                    type, info.global, {zero, field_index}, false,
                    "global.member.dtor.field");
                field->type = module_->types().pointer_type(
                    type->fields[position]);
                auto* index_slot = builder_.create_alloca(
                    int_type(), 0, "global.member.dtor.index");
                builder_.create_store(
                    module_->constant_int(int_type(), 0), index_slot);
                auto* condition = current_function_->add_block(
                    "global.member.dtor.cond");
                auto* body = current_function_->add_block(
                    "global.member.dtor.body");
                auto* done = current_function_->add_block(
                    "global.member.dtor.done");
                emit_branch(condition);
                builder_.set_insert_point(condition);
                auto* index = builder_.create_load(
                    int_type(), index_slot, false, 0,
                    "global.member.dtor.index.load");
                auto* limit = module_->constant_int(
                    int_type(),
                    static_cast<std::int64_t>(type->fields[position]->count));
                auto* compare = builder_.create_icmp(
                    ir::ICmpPredicate::Ult, index, limit,
                    "global.member.dtor.cmp");
                emit_conditional_branch(compare, body, done);
                builder_.set_insert_point(body);
                auto* element = builder_.create_gep(
                    type->fields[position], field, {zero, index}, false,
                    "global.member.dtor.element");
                element->type = module_->types().pointer_type(
                    type->fields[position]->element);
                auto* member_self = element;
                if (member_self->type !=
                    found->second->type->parameters[0]) {
                    member_self = builder_.create(
                        ir::Opcode::BitCast,
                        found->second->type->parameters[0], {member_self},
                        "global.member.dtor.this");
                }
                create_call_instruction(found->second, {member_self}, nullptr,
                                     "global.member.dtor");
                auto* next = builder_.create_add(
                    index, module_->constant_int(int_type(), 1),
                    "global.member.dtor.next");
                builder_.create_store(next, index_slot);
                emit_branch(condition);
                builder_.set_insert_point(done);
                continue;
            }
            if (!member.type ||
                member.type->kind != frontend::TypeKind::Record ||
                !member.type->record) {
                continue;
            }
            const auto member_methods =
                class_methods_.find(member.type->record->tag);
            if (member_methods == class_methods_.end()) {
                continue;
            }
            const auto found = member_methods->second.find(
                "~" + member.type->record->tag);
            if (found == member_methods->second.end()) {
                continue;
            }
            const auto position = field_offset + member_index - 1;
            if (position >= type->fields.size()) {
                continue;
            }
            auto* zero = module_->constant_int(int_type(), 0);
            auto* field_index = module_->constant_int(
                int_type(), static_cast<std::int64_t>(position));
            auto* field = builder_.create_gep(
                type, info.global, {zero, field_index}, false,
                "global.member.dtor.field");
            field->type = module_->types().pointer_type(
                type->fields[position]);
            auto* member_self = field;
            if (member_self->type !=
                found->second->type->parameters[0]) {
                member_self = builder_.create(
                    ir::Opcode::BitCast,
                    found->second->type->parameters[0], {member_self},
                    "global.member.dtor.this");
            }
            create_call_instruction(found->second, {member_self}, nullptr,
                                 "global.member.dtor");
        }
        if (base_destructor) {
            auto* base_self = static_cast<ir::Value*>(info.global);
            if (base_self->type !=
                base_destructor->type->parameters[0]) {
                base_self = builder_.create(
                    ir::Opcode::BitCast,
                    base_destructor->type->parameters[0], {base_self},
                    "global.base.dtor.this");
            }
            create_call_instruction(base_destructor, {base_self}, nullptr,
                                 "global.base.dtor");
        }
        builder_.create_ret(nullptr);
        scopes_ = std::move(previous_scopes);
        current_function_ = previous_function;
        current_class_ = previous_class;
        builder_.set_insert_point(previous_block);
        destructors.push_back(function);
    }
    for (const auto& [global, record] : local_static_dtors_) {
        if (!global || !record) {
            continue;
        }
        ir::Function* destructor = nullptr;
        for (auto* current = record; current != nullptr;
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
        if (!destructor) {
            continue;
        }
        auto* function = module_->add_function(
            "__krc_static_dtor_" + std::to_string(destructor_index++),
            void_function, ir::Linkage::Internal, false);
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
        auto* self = static_cast<ir::Value*>(global);
        if (self->type != destructor->type->parameters[0]) {
            self = builder_.create(ir::Opcode::BitCast,
                                   destructor->type->parameters[0], {self},
                                   "static.dtor.this");
        }
        create_call_instruction(destructor, {self}, nullptr, "static.dtor");
        builder_.create_ret(nullptr);
        scopes_ = std::move(previous_scopes);
        current_function_ = previous_function;
        current_class_ = previous_class;
        builder_.set_insert_point(previous_block);
        destructors.push_back(function);
    }

    auto* init_function = module_->add_function(
        "__krc_init_globals", void_function, ir::Linkage::Internal, false);
    const auto previous_function = current_function_;
    const auto previous_class = current_class_;
    auto* previous_block = builder_.insert_block();
    const auto previous_scopes = std::move(scopes_);
    scopes_.clear();
    current_function_ = init_function;
    current_class_ = nullptr;
    auto* entry = init_function->add_block("entry");
    builder_.reset_value_names();
    builder_.set_insert_point(entry);
    push_scope();
    for (auto* constructor : constructors) {
        create_call_instruction(constructor, {}, nullptr, "init.global");
    }
    for (const auto& info : global_initializers_) {
        if (!info.global || !info.expression) {
            continue;
        }
        if (info.expression->kind == frontend::AstKind::InitializerList) {
            if (emit_constant_global_initializer(*info.expression, info.global)) {
                continue;
            }
            emit_initializer(*info.expression, info.global,
                             info.global->element_type);
            continue;
        }
        long long constant = 0;
        if (info.global->element_type &&
            ir::is_integer(info.global->element_type) &&
            sema_.evaluate_constant_expression(*info.expression, constant)) {
            info.global->initializer =
                module_->constant_int(info.global->element_type, constant);
            continue;
        }
        auto value = emit_expression(*info.expression);
        if (!value.value) {
            continue;
        }
        builder_.create_store(
            promote(value.value, value.type, info.global->element_type),
            info.global);
    }
    builder_.create_ret(nullptr);
    scopes_ = std::move(previous_scopes);
    current_function_ = previous_function;
    current_class_ = previous_class;
    builder_.set_insert_point(previous_block);

    ir::Function* destroy_function = nullptr;
    if (!destructors.empty()) {
        destroy_function = module_->add_function(
            "__krc_destroy_globals", void_function, ir::Linkage::Internal,
            false);
        const auto previous_destroy_function = current_function_;
        const auto previous_destroy_class = current_class_;
        auto* previous_destroy_block = builder_.insert_block();
        const auto previous_destroy_scopes = std::move(scopes_);
        scopes_.clear();
        current_function_ = destroy_function;
        current_class_ = nullptr;
        auto* destroy_entry = destroy_function->add_block("entry");
        builder_.reset_value_names();
        builder_.set_insert_point(destroy_entry);
        push_scope();
        for (auto destructor = destructors.rbegin();
             destructor != destructors.rend(); ++destructor) {
            create_call_instruction(*destructor, {}, nullptr, "destroy.global");
        }
        builder_.create_ret(nullptr);
        scopes_ = std::move(previous_destroy_scopes);
        current_function_ = previous_destroy_function;
        current_class_ = previous_destroy_class;
        builder_.set_insert_point(previous_destroy_block);
    }

    const auto main_entry = functions_.find("main");
    if (main_entry == functions_.end()) {
        return;
    }
    auto* user_main = main_entry->second;
    user_main->name = "__krc_user_main";
    functions_.erase(main_entry);
    functions_["__krc_user_main"] = user_main;
    auto* wrapper =
        module_->add_function("main", user_main->type,
                              ir::Linkage::External, false);
    for (const auto& parameter : user_main->parameters) {
        wrapper->add_parameter(parameter->type, parameter->name);
    }
    const auto previous_block2 = builder_.insert_block();
    const auto previous_function2 = current_function_;
    const auto previous_class2 = current_class_;
    const auto previous_scopes2 = std::move(scopes_);
    scopes_.clear();
    current_function_ = wrapper;
    current_class_ = nullptr;
    auto* wrapper_entry = wrapper->add_block("entry");
    builder_.reset_value_names();
    builder_.set_insert_point(wrapper_entry);
    push_scope();
    create_call_instruction(init_function, {}, nullptr, "init.globals");
    std::vector<ir::Value*> arguments;
    for (const auto& parameter : wrapper->parameters) {
        arguments.push_back(parameter.get());
    }
    auto* call = create_call_instruction(user_main, std::move(arguments),
                                      user_main->type->return_type,
                                      "user.main");
    if (destroy_function) {
        create_call_instruction(destroy_function, {}, nullptr,
                             "destroy.globals");
    }
    if (ir::is_void(user_main->type->return_type)) {
        builder_.create_ret(nullptr);
    } else {
        builder_.create_ret(call);
    }
    scopes_ = std::move(previous_scopes2);
    current_function_ = previous_function2;
    current_class_ = previous_class2;
    builder_.set_insert_point(previous_block2);
}

void CIRGen::error(const frontend::SourceLocation& location, std::string message) {
    had_error_ = true;
    diagnostics_.report(frontend::Severity::Error, location, std::move(message));
}

ir::TypePtr CIRGen::map_type(frontend::TypePtr type) {
    if (!type) {
        return module_->types().int_type(static_cast<unsigned>(profile_.int_bits), true);
    }

    ir::TypePtr result = nullptr;
    switch (type->kind) {
    case frontend::TypeKind::Void:
        result = module_->types().void_type();
        break;
    case frontend::TypeKind::Bool:
        result = module_->types().bool_type();
        break;
    case frontend::TypeKind::Char:
        result = module_->types().int_type(8, !type->is_unsigned);
        break;
    case frontend::TypeKind::Short:
        result = module_->types().int_type(profile_.short_bits, !type->is_unsigned);
        break;
    case frontend::TypeKind::Int:
        result = module_->types().int_type(profile_.int_bits, !type->is_unsigned);
        break;
    case frontend::TypeKind::Long:
        result = module_->types().int_type(profile_.long_bits, !type->is_unsigned);
        break;
    case frontend::TypeKind::LongLong:
        result = module_->types().int_type(profile_.long_long_bits, !type->is_unsigned);
        break;
    case frontend::TypeKind::Float:
        result = module_->types().float_type(profile_.float_bits == 0 ? 32
                                                                      : profile_.float_bits);
        break;
    case frontend::TypeKind::Double:
        result = module_->types().float_type(profile_.double_bits == 0 ? 64
                                                                       : profile_.double_bits);
        break;
    case frontend::TypeKind::LongDouble:
        result = module_->types().float_type(profile_.long_double_bits == 0
                                                 ? 64
                                                 : profile_.long_double_bits);
        break;
    case frontend::TypeKind::Pointer:
        result = module_->types().pointer_type(map_type(type->element));
        break;
    case frontend::TypeKind::Array:
        result = module_->types().array_type(
            map_type(type->element), type->array_has_count ? type->array_count : 0);
        break;
    case frontend::TypeKind::Enum:
        result = module_->types().int_type(profile_.int_bits, true);
        break;
    case frontend::TypeKind::Function:
        result = map_function_type(type);
        break;
    case frontend::TypeKind::Record:
        result = type->record ? map_record(*type->record)
                              : module_->types().int_type(profile_.int_bits);
        break;
    }

    return result;
}

ir::TypePtr CIRGen::map_record(const frontend::RecordInfo& record) {
    const auto cached = record_types_.find(&record);
    if (cached != record_types_.end()) {
        return cached->second;
    }
    const auto name = record.tag.empty()
                          ? "anon." + std::to_string(record_counter_++)
                          : record.tag;
    const auto forward = module_->types().struct_forward(name);
    record_types_.emplace(&record, forward);
    ir_records_[forward] = &record;

    std::vector<ir::TypePtr> fields;
    std::vector<std::string> field_names;
    const bool base_polymorphic =
        record.base && record.base->kind == frontend::TypeKind::Record &&
        record.base->record && record.base->record->polymorphic;
    if (record.polymorphic && !base_polymorphic) {
        fields.push_back(module_->types().pointer_type(module_->types().int_type(8)));
        field_names.push_back("vptr");
    }
    std::string base_name;
    ir::TypePtr base_ir_type = nullptr;
    if (record.base && record.base->kind == frontend::TypeKind::Record &&
        record.base->record) {
        const auto base_type = map_record(*record.base->record);
        if (base_type) {
            base_name = base_type->name;
            base_ir_type = base_type;
        }
        for (std::size_t index = 0; base_type && index < base_type->fields.size(); ++index) {
            fields.push_back(base_type->fields[index]);
            field_names.push_back(index < base_type->field_names.size()
                                      ? "base." + base_type->field_names[index]
                                      : "base." + std::to_string(index));
        }
    }
    for (const auto& member : record.members) {
        if (!member.type) {
            continue;
        }
        fields.push_back(map_type(member.type));
        field_names.push_back(member.name);
    }
    if (!module_->types().complete_struct(forward, std::move(fields), std::move(field_names),
                                          false, std::move(base_name), base_ir_type)) {
        error(frontend::SourceLocation{}, "cannot complete struct type '" + name + "'");
    }
    return forward;
}

ir::TypePtr CIRGen::map_function_type(frontend::TypePtr type) {
    if (!type || type->kind != frontend::TypeKind::Function || !type->return_type) {
        return nullptr;
    }
    std::vector<ir::TypePtr> parameters;
    parameters.reserve(type->parameters.size());
    for (const auto& parameter : type->parameters) {
        const auto mapped = map_type(parameter);
        // A memory-class aggregate parameter is a hidden pointer to a copy the
        // caller owns; the callee copies it into its own frame slot.
        parameters.push_back(is_memory_class_argument(mapped)
                                 ? module_->types().pointer_type(mapped)
                                 : mapped);
    }
    return module_->types().function_type(map_type(type->return_type), std::move(parameters),
                                         type->variadic);
}

bool CIRGen::is_memory_class_argument(ir::TypePtr type) const {
    if (!type || (type->kind != ir::TypeKind::Struct && type->kind != ir::TypeKind::Union)) {
        return false;
    }
    // A target without a register path for aggregates passes every one of them as
    // a hidden pointer (schema's `aggregate_rules`). Deciding this from a size
    // threshold would make a by-value aggregate stop compiling on such a target
    // the moment its layout got smaller.
    if (profile_.aggregate_rules == target::AggregateRules::MemoryOnly) {
        return true;
    }
    const auto size = ir_size_of(type);
    if (size > 16) {
        return true;
    }
    if (size <= 8) {
        return false;
    }
    // Whether the aggregate is FP-only decides between two FPRs and memory.
    bool fp_only = !type->fields.empty();
    for (const auto& field : type->fields) {
        if (!field || field->kind != ir::TypeKind::Float) {
            fp_only = false;
            break;
        }
    }
    return !fp_only;
}

ir::TypePtr CIRGen::parameter_ir_type(ir::TypePtr mapped) const {
    if (is_memory_class_argument(mapped)) {
        return module_->types().pointer_type(mapped);
    }
    return mapped;
}

ir::TypePtr CIRGen::call_signature(ir::Value* callee) const {
    if (!callee || !callee->type) {
        return nullptr;
    }
    if (callee->type->kind == ir::TypeKind::Function) {
        return callee->type;
    }
    if (callee->type->kind == ir::TypeKind::Pointer && callee->type->element &&
        callee->type->element->kind == ir::TypeKind::Function) {
        return callee->type->element;
    }
    return nullptr;
}

bool CIRGen::has_label(const frontend::AstNode& specifiers, std::string_view label) const {
    for (const auto& entry : specifiers.labels) {
        if (entry == label) {
            return true;
        }
    }
    return false;
}

std::string CIRGen::declarator_name(const frontend::AstNode& declarator) const {
    if (declarator.kind == frontend::AstKind::Declarator) {
        return declarator.text;
    }
    if (declarator.children.empty()) {
        return {};
    }
    return declarator_name(*declarator.children.front());
}

const frontend::AstNode* CIRGen::find_function_declarator(
    const frontend::AstNode& declarator) const {
    const frontend::AstNode* current = &declarator;
    while (current) {
        if (is_function_type(*current)) {
            return current;
        }
        if (current->children.empty()) {
            return nullptr;
        }
        current = current->children.front().get();
    }
    return nullptr;
}

std::string CIRGen::parameter_name(const frontend::AstNode& parameter,
                                   std::size_t index) const {
    if (parameter.children.size() > 1 && parameter.children[1]) {
        const auto name = declarator_name(*parameter.children[1]);
        if (!name.empty()) {
            return name;
        }
    }
    return "arg" + std::to_string(index);
}

void CIRGen::collect_declarations() {
    // Template instantiations are analyzed outside the translation unit's
    // children, so they are registered before the definitions are generated
    // (ADR 0031).
    // An instantiated class's method bodies live in the clone, not in the
    // translation unit's children (ADR 0032).
    for (const auto* specifiers : sema_.class_instantiations()) {
        if (specifiers) {
            collect_class_methods(*specifiers);
        }
    }
    for (const auto* definition : sema_.instantiations()) {
        if (!definition || definition->children.size() < 3 ||
            !definition->children[0] || !definition->children[1] ||
            !definition->children[2]) {
            continue;
        }
        collect_function(*definition->children[0], *definition->children[1], true,
                         definition->children[2].get());
        definitions_.push_back(definition);
    }
    for (const auto& child : unit_.children) {
        if (!child) {
            continue;
        }
        if (child->kind == frontend::AstKind::FunctionDefinition ||
            child->kind == frontend::AstKind::Declaration) {
            if (!child->children.empty() && child->children[0]) {
                collect_class_methods(*child->children[0]);
            }
        }
        if (child->kind == frontend::AstKind::FunctionDefinition) {
            if (child->text.find("::") != std::string::npos) {
                collect_out_of_line_method(*child);
                continue;
            }
            if (child->children.size() < 3) {
                continue;
            }
            collect_function(*child->children[0], *child->children[1], true,
                             child->children[2].get());
            // A template's definition is emitted weak even when it is the
            // pattern: two translation units that instantiate the same all-int
            // case both define it, and a strong symbol in each is a duplicate.
            const bool template_definition =
                std::any_of(child->labels.begin(), child->labels.end(),
                            [](const std::string& label) {
                                return label.rfind("template-param:", 0) == 0 ||
                                       label.rfind("template-value-param:", 0) == 0;
                            });
            if (template_definition) {
                const auto defined = functions_.find(
                    declarator_name(*child->children[1]));
                if (defined != functions_.end() && defined->second) {
                    defined->second->linkage = ir::Linkage::Weak;
                }
            }
            definitions_.push_back(child.get());
        } else if (child->kind == frontend::AstKind::Declaration) {
            if (child->children.empty()) {
                continue;
            }
            for (std::size_t index = 1; index < child->children.size(); ++index) {
                const auto* init = child->children[index].get();
                if (!init || init->kind != frontend::AstKind::InitDeclarator ||
                    init->children.empty()) {
                    continue;
                }
                const auto* declarator = init->children.front().get();
                if (!declarator) {
                    continue;
                }
                const auto type = sema_.declaration_type(*child->children[0], declarator);
                if (!type) {
                    continue;
                }
                if (type->kind == frontend::TypeKind::Function) {
                    collect_function(*child->children[0], *declarator, false, nullptr);
                } else if (!has_label(*child->children[0], "typedef")) {
                    collect_global(*child->children[0], *declarator,
                                   init->children.size() > 1 ? init->children[1].get()
                                                             : nullptr);
                }
            }
        }
    }
}

void CIRGen::collect_out_of_line_method(const frontend::AstNode& definition) {
    if (definition.children.size() < 3 || !definition.children[0] ||
        !definition.children[1]) {
        return;
    }
    const auto separator = definition.text.rfind("::");
    if (separator == std::string::npos) {
        return;
    }
    const auto class_name = definition.text.substr(0, separator);
    const auto method_name = definition.text.substr(separator + 2);
    const auto record_separator = class_name.rfind("::");
    const auto record_name =
        record_separator == std::string::npos
            ? class_name
            : class_name.substr(record_separator + 2);
    const auto record_type = sema_.type_named(record_name);
    if (!record_type || record_type->kind != frontend::TypeKind::Record ||
        !record_type->record) {
        return;
    }
    auto frontend_type =
        sema_.declaration_type(*definition.children[0],
                               definition.children[1].get());
    if (!frontend_type || frontend_type->kind != frontend::TypeKind::Function) {
        return;
    }
    if (method_name.starts_with("operator ")) {
        const auto conversion =
            method_name.substr(9) == "bool"
                ? sema_.bool_type()
                : sema_.type_named(method_name.substr(9));
        if (conversion) {
            auto adjusted = std::make_shared<frontend::Type>(*frontend_type);
            adjusted->return_type = conversion;
            frontend_type = adjusted;
        }
    }
    auto* class_type = map_record(*record_type->record);
    std::vector<ir::TypePtr> parameters;
    parameters.push_back(module_->types().pointer_type(class_type));
    for (const auto& parameter : frontend_type->parameters) {
        parameters.push_back(parameter_ir_type(map_type(parameter)));
    }
    const auto ir_type = module_->types().function_type(
        map_type(frontend_type->return_type), std::move(parameters),
        frontend_type->variadic);
    const bool is_constructor = method_name == class_name;
    const auto parameter_count = frontend_type->parameters.size();
    const bool is_incdec = method_name == "operator++" ||
                           method_name == "operator--";
    const auto method_key =
        is_constructor
            ? method_name + "#" + std::to_string(parameter_count) + "#value"
            : is_incdec
                  ? method_name + "#" + std::to_string(parameter_count)
                  : method_name;
    auto& method_map = class_methods_[record_name];
    const auto existing = method_map.find(method_key);
    const bool reused_declaration =
        existing != method_map.end() && existing->second &&
        existing->second->declaration;
    ir::Function* function = nullptr;
    if (reused_declaration) {
        // The header declared this method for a caller in this translation
        // unit; the out-of-line definition now supplies its body.
        function = existing->second;
        function->declaration = false;
        function->linkage = ir::Linkage::Weak;
    } else {
        const std::string mangled =
            is_constructor
                ? record_name + "_ctor_" + std::to_string(parameter_count) + "_value"
                : mangle_method(record_name, method_name);
        function = module_->add_function(mangled, ir_type, ir::Linkage::Weak, false);
        function->debug_name =
            is_constructor ? record_name + "::" + class_name
                           : record_name + "::" + method_name;
        function->add_parameter(module_->types().pointer_type(class_type), "this");
        const auto* function_declarator =
            find_function_declarator(*definition.children[1]);
        std::size_t parameter_index = 0;
        if (function_declarator) {
            for (std::size_t index = 1; index < function_declarator->children.size();
                 ++index) {
                const auto* parameter = function_declarator->children[index].get();
                if (!parameter ||
                    parameter->kind != frontend::AstKind::ParameterDeclaration ||
                    parameter_index >= frontend_type->parameters.size()) {
                    continue;
                }
                function->add_parameter(
                    parameter_ir_type(map_type(frontend_type->parameters[parameter_index])),
                    parameter_name(*parameter, parameter_index));
                parameter_index++;
            }
        }
        method_map[method_key] = function;
    }
    if (has_label(*definition.children[0], "static")) {
        static_methods_.insert(function);
    }
    if (is_constructor) {
        auto& constructors = class_constructors_[record_name];
        if (std::find(constructors.begin(), constructors.end(), function) ==
            constructors.end()) {
            constructors.push_back(function);
        }
    } else if (is_incdec) {
        class_incdec_[record_name].push_back(function);
    }
    function_ast_types_[function] = frontend_type;
    function_source_names_[function] = method_name;
    function_classes_[function] = record_type->record.get();
    method_bodies_.push_back({function, &definition});
}

void CIRGen::collect_class_methods(const frontend::AstNode& specifiers) {
    for (const auto& child : specifiers.children) {
        if (!child || child->kind != frontend::AstKind::StructSpecifier ||
            child->text.empty()) {
            continue;
        }
        const auto record_type = sema_.type_named(child->text);
        if (!record_type || record_type->kind != frontend::TypeKind::Record ||
            !record_type->record) {
            continue;
        }
        const auto* record = record_type->record.get();
        auto* class_type = map_record(*record);
        auto& methods = class_methods_[child->text];
        // A name declared twice is a real overload pair, and only those pay for
        // the constness suffix. A single-method class keeps its plain key, so a
        // const conversion operator or a lone size() is untouched.
        std::unordered_map<std::string, int> name_counts;
        for (const auto& recorded : record->methods) {
            ++name_counts[recorded.name];
        }
        std::unordered_set<std::string> overloaded_names;
        for (const auto& entry : name_counts) {
            if (entry.second > 1) {
                overloaded_names.insert(entry.first);
            }
        }
        for (const auto& member : child->children) {
            if (member && member->kind == frontend::AstKind::MemberDeclaration &&
                member->children.size() >= 2 && member->children[0] &&
                member->children[1] &&
                has_label(*member->children[0], "static") &&
                member->children.size() >= 3 && member->children[2] &&
                !member->children[2]->children.empty() &&
                member->children[2]->children.front()) {
                frontend::AstNode declarator;
                declarator.kind = frontend::AstKind::Declarator;
                declarator.location = member->children[1]->location;
                declarator.text =
                    child->text + "::" +
                    declarator_name(*member->children[1]);
                collect_global(*member->children[0], declarator,
                               member->children[2]->children.front().get());
            }
            // A member function declared without a body still has to exist as
            // an external IR function: its definition may live in another
            // translation unit, and a caller here must be able to call it.
            if (member && member->kind == frontend::AstKind::MemberDeclaration &&
                member->children.size() >= 2 && member->children[0] &&
                member->children[1] &&
                find_function_declarator(*member->children[1]) != nullptr) {
                const auto declared_name = declarator_name(*member->children[1]);
                const bool is_pure =
                    std::find(member->labels.begin(), member->labels.end(), "pure") !=
                    member->labels.end();
                const bool is_constructor = declared_name == record->tag;
                auto declared_type = sema_.declaration_type(
                    *member->children[0], member->children[1].get());
                const auto declared_parameters =
                    declared_type && declared_type->kind == frontend::TypeKind::Function
                        ? declared_type->parameters.size()
                        : 0;
                const std::string declared_key =
                    is_constructor
                        ? declared_name + "#" +
                              std::to_string(declared_parameters) + "#value"
                        : declared_name;
                const bool defaulted_or_deleted =
                    std::find(member->labels.begin(), member->labels.end(),
                              "defaulted") != member->labels.end() ||
                    std::find(member->labels.begin(), member->labels.end(),
                              "deleted") != member->labels.end();
                if (!declared_name.empty() && !declared_name.starts_with("~") &&
                    !is_pure &&
                    !(is_constructor && defaulted_or_deleted) &&
                    methods.find(declared_key) == methods.end()) {
                    if (declared_type && declared_type->kind == frontend::TypeKind::Function) {
                        std::vector<ir::TypePtr> parameters;
                        parameters.push_back(module_->types().pointer_type(class_type));
                        for (const auto& parameter : declared_type->parameters) {
                            parameters.push_back(
                                parameter_ir_type(map_type(parameter)));
                        }
                        const auto ir_type = module_->types().function_type(
                            map_type(declared_type->return_type), std::move(parameters),
                            declared_type->variadic);
                        const std::string mangled =
                            is_constructor
                                ? record->tag + "_ctor_" +
                                      std::to_string(declared_parameters) + "_value"
                                : mangle_method(record->tag, declared_name);
                        auto* declaration = module_->add_function(
                            mangled, ir_type, ir::Linkage::External, true);
                        declaration->debug_name = record->tag + "::" + declared_name;
                        if (has_label(*member->children[0], "static")) {
                            static_methods_.insert(declaration);
                        }
                        declaration->add_parameter(
                            module_->types().pointer_type(class_type), "this");
                        const auto* declared_declarator =
                            find_function_declarator(*member->children[1]);
                        std::size_t declared_parameter = 0;
                        if (declared_declarator) {
                            for (std::size_t index = 1;
                                 index < declared_declarator->children.size(); ++index) {
                                const auto* parameter =
                                    declared_declarator->children[index].get();
                                if (!parameter ||
                                    parameter->kind !=
                                        frontend::AstKind::ParameterDeclaration ||
                                    declared_parameter >= declared_type->parameters.size()) {
                                    continue;
                                }
                                declaration->add_parameter(
                                    parameter_ir_type(map_type(
                                        declared_type->parameters[declared_parameter])),
                                    parameter_name(*parameter, declared_parameter));
                                declared_parameter++;
                            }
                        }
                        class_methods_[record->tag][declared_key] = declaration;
                        if (is_constructor) {
                            class_constructors_[record->tag].push_back(declaration);
                        }
                        function_ast_types_[declaration] = declared_type;
                        function_source_names_[declaration] = declared_name;
                        function_classes_[declaration] = record;
                    }
                }
            }
            if (!member || member->kind != frontend::AstKind::FunctionDefinition ||
                member->children.size() < 3 || !member->children[0] ||
                !member->children[1]) {
                continue;
            }
            const auto method_name = declarator_name(*member->children[1]);
            const bool is_constructor = method_name == record->tag;
            const bool is_assignment = method_name == "operator=";
            const bool is_incdec = method_name == "operator++" ||
                                   method_name == "operator--";
            // Only a genuine overload pair is split by constness; every other
            // method keeps the plain key it has always had.
            const bool pair_split = overloaded_names.contains(method_name);
            const bool method_is_const =
                pair_split && declarator_is_const(*member->children[1]);
            if (method_name.empty() ||
                (!is_constructor && !is_assignment && !is_incdec &&
                 methods.contains(method_key_of(method_name, method_is_const)))) {
                continue;
            }
            // The record's method type was resolved when the class was
            // analysed, which is the only time a template instance's bindings
            // are active; re-deriving it here would resolve T to the
            // pattern's stub (ADR 0032).
            auto frontend_type = sema_.declaration_type(*member->children[0],
                                                        member->children[1].get());
            if (frontend_type && frontend_type->kind == frontend::TypeKind::Function &&
                sema_.is_class_instance(record->tag)) {
                // A class can record several methods under one name and arity: a
                // copy constructor and a move constructor are both `Box#1`. The
                // declaration's own declarator says which one this is, so the
                // recorded type is matched on the parameter's value category.
                // Without that, both declarations took the first recorded type
                // and emitted the same `_rvalue` symbol, so the copy constructor
                // was never generated (build/corpus/dup4.cpp).
                bool declared_rvalue = false;
                std::size_t declared_parameters = 0;
                for (const auto* cursor = member->children[1].get(); cursor != nullptr;) {
                    if (cursor->kind == frontend::AstKind::FunctionDeclarator) {
                        for (const auto& parameter : cursor->children) {
                            if (!parameter ||
                                parameter->kind !=
                                    frontend::AstKind::ParameterDeclaration) {
                                continue;
                            }
                            if (declared_parameters == 0) {
                                // The reference declarator sits beside the
                                // specifiers, so the whole subtree is walked.
                                const auto marks_rvalue =
                                    [](const frontend::AstNode* node,
                                       const auto& self) -> bool {
                                    if (node == nullptr) {
                                        return false;
                                    }
                                    if (node->kind ==
                                            frontend::AstKind::ReferenceDeclarator &&
                                        std::find(node->labels.begin(),
                                                  node->labels.end(),
                                                  "rvalue") != node->labels.end()) {
                                        return true;
                                    }
                                    for (const auto& child_node : node->children) {
                                        if (self(child_node.get(), self)) {
                                            return true;
                                        }
                                    }
                                    return false;
                                };
                                declared_rvalue =
                                    marks_rvalue(parameter.get(), marks_rvalue);
                            }
                            ++declared_parameters;
                        }
                        break;
                    }
                    cursor = cursor->children.empty() ? nullptr
                                                      : cursor->children.front().get();
                }
                for (const auto& recorded : record->methods) {
                    if (recorded.name != method_name || !recorded.type ||
                        recorded.type->kind != frontend::TypeKind::Function) {
                        continue;
                    }
                    if (recorded.type->parameters.size() != declared_parameters) {
                        continue;
                    }
                    const bool recorded_rvalue =
                        !recorded.type->parameters.empty() &&
                        recorded.type->parameters.front() &&
                        recorded.type->parameters.front()->is_rvalue_reference;
                    if (recorded_rvalue != declared_rvalue) {
                        continue;
                    }
                    frontend_type = recorded.type;
                    break;
                }
            }
            if (!frontend_type || frontend_type->kind != frontend::TypeKind::Function) {
                continue;
            }
            if (method_name.starts_with("operator ")) {
                const auto conversion =
                    method_name.substr(9) == "bool"
                        ? sema_.bool_type()
                        : sema_.type_named(method_name.substr(9));
                if (conversion) {
                    auto adjusted =
                        std::make_shared<frontend::Type>(*frontend_type);
                    adjusted->return_type = conversion;
                    frontend_type = adjusted;
                }
            }
            const auto parameter_count = frontend_type->parameters.size();
            const auto first_parameter =
                frontend_type->parameters.empty() ? nullptr
                                                  : frontend_type->parameters.front();
            const std::string constructor_kind =
                first_parameter && first_parameter->is_reference
                    ? (first_parameter->is_rvalue_reference ? "rvalue" : "lvalue")
                    : "value";
            const std::string assignment_kind =
                first_parameter && first_parameter->is_reference
                    ? (first_parameter->is_rvalue_reference ? "rvalue" : "lvalue")
                    : "value";
            std::vector<ir::TypePtr> parameters;
            parameters.push_back(module_->types().pointer_type(class_type));
            for (const auto& parameter : frontend_type->parameters) {
                parameters.push_back(parameter_ir_type(map_type(parameter)));
            }
            const auto ir_type = module_->types().function_type(
                map_type(frontend_type->return_type), std::move(parameters),
                frontend_type->variadic);
            const auto mangled =
                is_constructor
                    ? record->tag + "_ctor_" + std::to_string(parameter_count) +
                          "_" + constructor_kind
                    : is_assignment
                          ? record->tag + "_op_eq_" + assignment_kind
                          : is_incdec
                                ? record->tag + "_op_inc_" +
                                      std::to_string(parameter_count)
                                : method_symbol(child->text, method_name,
                                                 method_is_const);
            auto* function =
                module_->add_function(mangled, ir_type, ir::Linkage::Weak, false);
            function->debug_name =
                is_constructor ? record->tag + "::" + record->tag
                               : record->tag + "::" + method_name;
            if (has_label(*member->children[0], "static")) {
                static_methods_.insert(function);
            }
            function->add_parameter(module_->types().pointer_type(class_type), "this");
            const auto* function_declarator =
                find_function_declarator(*member->children[1]);
            std::size_t parameter_index = 0;
            if (function_declarator) {
                for (std::size_t index = 1; index < function_declarator->children.size();
                     ++index) {
                    const auto* parameter = function_declarator->children[index].get();
                    if (!parameter ||
                        parameter->kind != frontend::AstKind::ParameterDeclaration ||
                        parameter_index >= frontend_type->parameters.size()) {
                        continue;
                    }
                    function->add_parameter(
                        parameter_ir_type(map_type(frontend_type->parameters[parameter_index])),
                        parameter_name(*parameter, parameter_index));
                    parameter_index++;
                }
            }
            const auto method_key =
                is_constructor
                    ? method_name + "#" + std::to_string(parameter_count) + "#" +
                          constructor_kind
                    : is_assignment
                          ? method_name + "#" + assignment_kind
                          : is_incdec
                                ? method_name + "#" +
                                      std::to_string(parameter_count)
                                : method_key_of(method_name, method_is_const);
            methods.emplace(method_key, function);
            if (is_constructor) {
                class_constructors_[record->tag].push_back(function);
            } else if (is_assignment) {
                class_assignments_[record->tag].push_back(function);
            } else if (is_incdec) {
                class_incdec_[record->tag].push_back(function);
            }
            record_defaults(function, *member->children[1]);
            function_ast_types_[function] = frontend_type;
            function_source_names_[function] = method_name;
            function_classes_[function] = record;
            if (sema_.is_class_instance(child->text)) {
                instance_members_.insert(function);
            } else {
                // A class template's own record is the unparameterised spelling,
                // so its bodies are written for stub types and cannot be built.
                // Only the instance's copies are (ADR 0032).
                const bool pattern_member = std::any_of(
                    child->labels.begin(), child->labels.end(),
                    [](const std::string& label) {
                        return label.rfind("template-param:", 0) == 0 ||
                               label.rfind("template-value-param:", 0) == 0;
                    });
                if (pattern_member) {
                    function->declaration = true;
                    continue;
                }
            }
            method_bodies_.push_back({function, member.get()});
        }
        for (const auto& method : record->methods) {
            if (!method.is_pure || method.name.starts_with("~") ||
                methods.contains(method.name)) {
                continue;
            }
            std::vector<ir::TypePtr> parameters{
                module_->types().pointer_type(class_type)};
            for (const auto& parameter : method.type->parameters) {
                parameters.push_back(parameter_ir_type(map_type(parameter)));
            }
            const auto ir_type = module_->types().function_type(
                map_type(method.type->return_type), std::move(parameters),
                method.type->variadic);
            auto* function = module_->add_function(
                mangle_method(record->tag, method.name), ir_type,
                ir::Linkage::Weak, false);
            function->declaration = true;
            function->add_parameter(module_->types().pointer_type(class_type),
                                    "this");
            for (std::size_t index = 0;
                 index < method.type->parameters.size(); ++index) {
                function->add_parameter(
                    parameter_ir_type(map_type(method.type->parameters[index])),
                    "parameter" + std::to_string(index));
            }
            function_ast_types_[function] = method.type;
            function_source_names_[function] = method.name;
            methods.emplace(method.name, function);
        }
    }
}

void CIRGen::clone_inherited_methods() {
    std::unordered_map<const ir::Function*, const frontend::AstNode*> bodies;
    for (const auto& [function, definition] : method_bodies_) {
        bodies[function] = definition;
    }
    std::vector<std::pair<ir::Function*, const frontend::AstNode*>> additions;
    std::vector<std::pair<ir::Function*, const frontend::RecordInfo*>> class_additions;
    for (const auto& [type, record] : ir_records_) {
        static_cast<void>(type);
        if (!record || !record->base || record->base->kind != frontend::TypeKind::Record ||
            !record->base->record) {
            continue;
        }
        const auto base_methods = class_methods_.find(record->base->record->tag);
        if (base_methods == class_methods_.end()) {
            continue;
        }
        const auto base_methods_copy = base_methods->second;
        auto& derived_methods = class_methods_[record->tag];
        for (const auto& [name, base_function] : base_methods_copy) {
            if (name == record->base->record->tag ||
                name.starts_with(record->base->record->tag + "#") ||
                derived_methods.contains(name)) {
                continue;
            }
            const auto body = bodies.find(base_function);
            if (body == bodies.end()) {
                continue;
            }
            auto* clone = module_->add_function(
                mangle_method(record->tag, name), base_function->type,
                ir::Linkage::Weak, false);
            for (const auto& parameter : base_function->parameters) {
                clone->add_parameter(parameter->type, parameter->name);
            }
            clone->debug_name = base_function->debug_name;
            derived_methods.emplace(name, clone);
            function_source_names_[clone] = name;
            functions_[clone->name] = clone;
            additions.push_back({clone, body->second});
            class_additions.push_back({clone, record});
        }
    }
    for (const auto& [function, definition] : additions) {
        method_bodies_.push_back({function, definition});
    }
    for (const auto& [function, record] : class_additions) {
        function_classes_[function] = record;
    }
}

void CIRGen::record_defaults(ir::Function* function,
                             const frontend::AstNode& declarator) {
    const auto* function_declarator = find_function_declarator(declarator);
    if (!function_declarator) {
        return;
    }
    std::vector<const frontend::AstNode*> defaults;
    for (std::size_t index = 1; index < function_declarator->children.size(); ++index) {
        const auto* parameter = function_declarator->children[index].get();
        if (!parameter || parameter->kind != frontend::AstKind::ParameterDeclaration) {
            continue;
        }
        defaults.push_back(parameter->children.size() > 2 && parameter->children[2]
                               ? parameter->children[2].get()
                               : nullptr);
    }
    if (std::any_of(defaults.begin(), defaults.end(),
                    [](const frontend::AstNode* value) { return value != nullptr; })) {
        function_defaults_[function] = std::move(defaults);
    }
}

ir::Function* CIRGen::select_constructor(const std::string& tag,
                                         std::size_t argument_count,
                                         int reference_mode) const {
    const auto found = class_constructors_.find(tag);
    if (found == class_constructors_.end()) {
        return nullptr;
    }
    ir::Function* ranged = nullptr;
    ir::Function* fallback = nullptr;
    for (auto* function : found->second) {
        if (!function || !function->type ||
            function->type->parameters.empty()) {
            continue;
        }
        const auto total = function->type->parameters.size() - 1;
        std::size_t defaults = 0;
        const auto ast_type = function_ast_types_.find(function);
        if (ast_type != function_ast_types_.end() && ast_type->second) {
            defaults = ast_type->second->default_argument_count;
        }
        const auto required = total >= defaults ? total - defaults : 0;
        const bool reference_parameter =
            ast_type != function_ast_types_.end() && ast_type->second &&
            !ast_type->second->parameters.empty() &&
            ast_type->second->parameters.front() &&
            ast_type->second->parameters.front()->is_reference;
        const bool rvalue_parameter =
            reference_parameter &&
            ast_type->second->parameters.front()->is_rvalue_reference;
        if (argument_count == total) {
            if (reference_mode == 0 && !reference_parameter) {
                return function;
            }
            if (reference_mode == 1 && reference_parameter && !rvalue_parameter) {
                return function;
            }
            if (reference_mode == 2 && rvalue_parameter) {
                return function;
            }
            // A mode of 1 or 2 says the argument *is* a reference, so a parameter that
            // is not one is not a candidate at all. Recording it as a fallback anyway
            // made `Counter copy(original)` select `explicit Counter(int)` and pass the
            // record where an int belonged -- the `call argument type mismatch` the IR
            // verifier reported. Only an unconstrained request keeps a fallback.
            if (!fallback && reference_mode == 0) {
                fallback = function;
            }
        }
        // `ranged` is the default-argument match, and it too honours the mode: a
        // caller that says the argument is a reference has already ruled out every
        // constructor whose parameter is not one.
        if (reference_mode == 0 && argument_count >= required &&
            argument_count <= total) {
            ranged = function;
        }
    }
    if (fallback) {
        return fallback;
    }
    return ranged;
}

ir::Function* CIRGen::select_assignment(const std::string& tag,
                                        int reference_mode) const {
    const auto found = class_assignments_.find(tag);
    if (found == class_assignments_.end()) {
        return nullptr;
    }
    for (auto* function : found->second) {
        if (!function || !function->type) {
            continue;
        }
        const auto ast_type = function_ast_types_.find(function);
        if (ast_type == function_ast_types_.end() || !ast_type->second ||
            ast_type->second->parameters.empty() ||
            !ast_type->second->parameters.front()) {
            continue;
        }
        const auto& parameter = *ast_type->second->parameters.front();
        if (reference_mode == 2 && parameter.is_rvalue_reference) {
            return function;
        }
        if (reference_mode == 1 && parameter.is_reference &&
            !parameter.is_rvalue_reference) {
            return function;
        }
        if (reference_mode == 0 && !parameter.is_reference) {
            return function;
        }
    }
    return nullptr;
}

void CIRGen::collect_function(const frontend::AstNode& specifiers,
                              const frontend::AstNode& declarator, bool definition,
                              const frontend::AstNode* body) {
    const auto name = declarator_name(declarator);
    if (name.empty()) {
        error(declarator.location, "function has no name");
        return;
    }
    auto frontend_type = sema_.declaration_type(specifiers, &declarator);
    if (!frontend_type || frontend_type->kind != frontend::TypeKind::Function) {
        error(declarator.location, "invalid function type");
        return;
    }
    if (const auto symbol = sema_.global_symbol(name);
        symbol && symbol->kind == frontend::Sema::GlobalSymbol::Kind::Function &&
        symbol->type && symbol->type->kind == frontend::TypeKind::Function) {
        frontend_type = symbol->type;
    }
    const auto ir_type = map_function_type(frontend_type);
    if (!ir_type) {
        error(declarator.location, "cannot map function type to IR");
        return;
    }

    ir::Function* function = nullptr;
    const auto existing = functions_.find(name);
    if (existing != functions_.end()) {
        function = existing->second;
        if (definition && !function->declaration && !function->blocks.empty()) {
            error(declarator.location, "redefinition of function '" + name + "'");
            return;
        }
        if (definition) {
            function->declaration = false;
        }
    } else {
        const bool internal = has_label(specifiers, "static");
        const bool weak = has_label(specifiers, "inline") ||
                          has_label(specifiers, "constexpr");
        function = module_->add_function(
            name.starts_with("operator") ? mangle_method("", name) : name, ir_type,
            weak ? ir::Linkage::Weak
                 : internal ? ir::Linkage::Internal : ir::Linkage::External,
            !definition);
        function->debug_name = name;
        functions_.emplace(name, function);
    }

    const auto* function_declarator = find_function_declarator(declarator);
    std::size_t parameter_index = 0;
    record_defaults(function, declarator);
    function_ast_types_[function] = frontend_type;
    function_source_names_[function] = name;
    if (function_declarator) {
        for (std::size_t index = 1; index < function_declarator->children.size(); ++index) {
            const auto* parameter = function_declarator->children[index].get();
            if (!parameter || parameter->kind != frontend::AstKind::ParameterDeclaration) {
                continue;
            }
            if (parameter_index >= frontend_type->parameters.size()) {
                break;
            }
            const auto parameter_type =
                parameter_ir_type(map_type(frontend_type->parameters[parameter_index]));
            const auto parameter_text = parameter_name(*parameter, parameter_index);
            if (parameter_index < function->parameters.size()) {
                function->parameters[parameter_index]->name = parameter_text;
                function->parameters[parameter_index]->type = parameter_type;
            } else if (parameter_index < frontend_type->parameters.size()) {
                function->add_parameter(parameter_type, parameter_text);
            }
            ++parameter_index;
        }
    }
    for (; parameter_index < frontend_type->parameters.size(); ++parameter_index) {
        if (parameter_index >= function->parameters.size()) {
            function->add_parameter(parameter_ir_type(map_type(frontend_type->parameters[parameter_index])),
                                    "arg" + std::to_string(parameter_index));
        }
    }
    static_cast<void>(body);
}

void CIRGen::collect_global(const frontend::AstNode& specifiers,
                            const frontend::AstNode& declarator,
                            const frontend::AstNode* initializer) {
    const auto name = declarator_name(declarator);
    if (name.empty()) {
        return;
    }
    const bool external = has_label(specifiers, "extern") && initializer == nullptr;
    auto existing = globals_.find(name);
    if (existing != globals_.end()) {
        if (!existing->second->external || external) {
            return;
        }
        existing->second->external = false;
    }
    auto frontend_type = sema_.declaration_type(specifiers, &declarator);
    if (!frontend_type) {
        return;
    }
    if (frontend_type->kind == frontend::TypeKind::Array &&
        !frontend_type->array_has_count && initializer &&
        initializer->kind == frontend::AstKind::StringLiteral) {
        const auto bytes = decode_string(initializer->text);
        auto inferred = std::make_shared<frontend::Type>(*frontend_type);
        inferred->array_count = bytes.size() + 1;
        inferred->array_has_count = true;
        frontend_type = inferred;
    } else if (frontend_type->kind == frontend::TypeKind::Array &&
               !frontend_type->array_has_count && initializer &&
               initializer->kind == frontend::AstKind::InitializerList) {
        auto inferred = std::make_shared<frontend::Type>(*frontend_type);
        inferred->array_count = initializer->children.size();
        inferred->array_has_count = true;
        frontend_type = inferred;
    }
    const auto type = map_type(frontend_type);
    const auto alignment = alignment_of(specifiers, frontend_type);
    std::string symbol_name = name;
    for (std::size_t separator = symbol_name.find("::");
         separator != std::string::npos;
         separator = symbol_name.find("::", separator)) {
        symbol_name.replace(separator, 2, "__");
    }
    auto* global = existing != globals_.end()
                       ? existing->second
                       : module_->add_global(symbol_name, type,
                                             has_label(specifiers, "const"),
                                             alignment);
    if (existing == globals_.end()) {
        globals_.emplace(name, global);
    }
    global->external = external;
    global->internal = has_label(specifiers, "static") &&
                       name.find("::") == std::string::npos;
    global_frontend_types_[global] = frontend_type;
    if (frontend_type->is_volatile) {
        volatile_globals_.insert(global);
    }
    if (external) {
        return;
    }
    if (type->kind == ir::TypeKind::Struct && frontend_type->record) {
        bool has_destructor = false;
        for (auto* current = frontend_type->record.get(); current != nullptr;
             current = current->base && current->base->record
                           ? current->base->record.get()
                           : nullptr) {
            const auto methods = class_methods_.find(current->tag);
            if (methods == class_methods_.end()) {
                continue;
            }
            has_destructor =
                methods->second.find("~" + current->tag) !=
                methods->second.end();
            if (has_destructor) {
                break;
            }
        }
        const bool has_constructor =
            select_constructor(frontend_type->record->tag, 0) != nullptr;
        bool has_member_initialization = false;
        for (const auto& member : frontend_type->record->members) {
            if (member.default_initializer) {
                has_member_initialization = true;
            }
            if (member.type &&
                member.type->kind == frontend::TypeKind::Record &&
                member.type->record &&
                select_constructor(member.type->record->tag, 0)) {
                has_member_initialization = true;
            }
            if (member.type &&
                member.type->kind == frontend::TypeKind::Array &&
                member.type->element && member.type->element->record &&
                select_constructor(member.type->element->record->tag, 0)) {
                has_member_initialization = true;
            }
        }
        const bool has_base_initialization =
            frontend_type->record->base &&
            frontend_type->record->base->kind ==
                frontend::TypeKind::Record &&
            frontend_type->record->base->record &&
            select_constructor(
                frontend_type->record->base->record->tag, 0) != nullptr;
        if (has_constructor || has_destructor ||
            has_member_initialization || has_base_initialization) {
            GlobalCtorInfo info;
            info.global = global;
            info.record = frontend_type->record.get();
            info.default_construct = !has_constructor;
            global_ctors_.push_back(std::move(info));
            return;
        }
    }
    if (type->kind == ir::TypeKind::Struct && frontend_type->record &&
        initializer &&
        initializer->kind == frontend::AstKind::ExpressionStatement) {
        GlobalCtorInfo info;
        info.global = global;
        info.record = frontend_type->record.get();
        for (const auto& argument : initializer->children) {
            info.arguments.push_back(argument.get());
        }
        if (select_constructor(info.record->tag, info.arguments.size())) {
            global_ctors_.push_back(std::move(info));
            return;
        }
    }
    if (!initializer) {
        return;
    }
    if (type->kind == ir::TypeKind::Array && type->element &&
        ir::is_integer(type->element) && type->element->bits == 8 &&
        initializer->kind == frontend::AstKind::StringLiteral) {
        const auto bytes = decode_string(initializer->text);
        std::vector<std::uint8_t> data(type->count, 0);
        for (std::size_t index = 0; index < data.size() &&
                                    index < bytes.size();
             ++index) {
            data[index] = static_cast<std::uint8_t>(bytes[index]);
        }
        global->initializer =
            module_->constant_data_array(type, std::move(data));
        return;
    }
    if (type->kind == ir::TypeKind::Struct && initializer &&
        initializer->kind == frontend::AstKind::InitializerList) {
        global_initializers_.push_back({global, initializer});
        return;
    }
    if (type->kind == ir::TypeKind::Array && initializer &&
        initializer->kind == frontend::AstKind::InitializerList) {
        global_initializers_.push_back({global, initializer});
        return;
    }
    switch (initializer->kind) {
    case frontend::AstKind::IntegerLiteral: {
        long long value = 0;
        if (sema_.evaluate_constant_expression(*initializer, value) && ir::is_integer(type)) {
            global->initializer = module_->constant_int(type, value);
        }
        break;
    }
    case frontend::AstKind::FloatLiteral: {
        if (ir::is_float(type)) {
            global->initializer =
                module_->constant_float(type, std::stod(initializer->text));
        }
        break;
    }
    default: {
        long long value = 0;
        if (ir::is_integer(global->element_type) &&
            sema_.evaluate_constant_expression(*initializer, value)) {
            global->initializer = module_->constant_int(global->element_type, value);
            break;
        }
        if (ir::is_integer(global->element_type) ||
            ir::is_pointer(global->element_type) ||
            ir::is_float(global->element_type)) {
            global_initializers_.push_back({global, initializer});
            break;
        }
        error(initializer->location, "global initializer is not supported yet");
        break;
    }
    }
}

void CIRGen::push_scope() { scopes_.emplace_back(); }

void CIRGen::pop_scope() {
    if (!scopes_.empty()) {
        scopes_.pop_back();
    }
}

CIRGen::LocalBinding* CIRGen::lookup_local(std::string_view name) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        const auto found = scope->find(std::string(name));
        if (found != scope->end()) {
            return &found->second;
        }
    }
    const auto separator = name.rfind("::");
    if (separator != std::string_view::npos) {
        return lookup_local(name.substr(separator + 2));
    }
    return nullptr;
}

ir::Function* CIRGen::lookup_function(std::string_view name) const {
    const auto found = functions_.find(std::string(name));
    if (found != functions_.end()) {
        return found->second;
    }
    const auto separator = name.rfind("::");
    if (separator != std::string_view::npos) {
        return lookup_function(name.substr(separator + 2));
    }
    return nullptr;
}

ir::GlobalVariable* CIRGen::lookup_global(std::string_view name) const {
    const auto found = globals_.find(std::string(name));
    if (found != globals_.end()) {
        return found->second;
    }
    const auto separator = name.rfind("::");
    if (separator != std::string_view::npos) {
        return lookup_global(name.substr(separator + 2));
    }
    return nullptr;
}

void CIRGen::declare_local(std::string name, LocalBinding binding) {
    scopes_.back()[std::move(name)] = std::move(binding);
}

std::uint32_t CIRGen::alignment_of(frontend::TypePtr type) const {
    return type ? static_cast<std::uint32_t>(sema_.align_of_type(type)) : 1;
}

std::uint32_t CIRGen::alignment_of(const frontend::AstNode& specifiers,
                                   frontend::TypePtr type) const {
    auto alignment = alignment_of(type);
    for (const auto& child : specifiers.children) {
        if (!child || child->kind != frontend::AstKind::AlignasSpecifier ||
            child->children.empty() || !child->children.front()) {
            continue;
        }
        long long value = 0;
        if (sema_.evaluate_constant_expression(*child->children.front(), value) &&
            value > 0) {
            alignment = std::max(alignment, static_cast<std::uint32_t>(value));
        }
    }
    return alignment;
}

ir::TypePtr CIRGen::size_type() const {
    return module_->types().int_type(profile_.size_t_bits, false);
}

ir::TypePtr CIRGen::int_type() const {
    return module_->types().int_type(profile_.int_bits, true);
}


}  // namespace krc::irgen
