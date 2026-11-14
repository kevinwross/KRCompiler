#include "irgen/c_irgen.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace krc::irgen {
// A reference to a record names the record it refers to.
frontend::TypePtr record_object_type(frontend::TypePtr type) {
    if (type && type->is_reference && type->element) {
        return type->element;
    }
    return type;
}


namespace {

bool is_function_type(const frontend::AstNode& node) {
    return node.kind == frontend::AstKind::FunctionDeclarator;
}

bool is_move_call(const frontend::AstNode& node) {
    return node.kind == frontend::AstKind::CallExpression && !node.children.empty() &&
           node.children.front() &&
           node.children.front()->kind == frontend::AstKind::Identifier &&
           (node.children.front()->text == "std::move" ||
            node.children.front()->text == "std::forward") && node.children.size() > 1 &&
           node.children[1];
}

// True when `type` is a pointer to a record holding `lanes` f32 values, the
// shape a mat4 (16 lanes) or vec4 (4 lanes) lowers to.
bool points_to_float_lane_record(ir::TypePtr type, std::size_t lanes) {
    return type && type->kind == ir::TypeKind::Pointer &&
           ir::is_float_lane_record(type->element, lanes);
}

// Maps a declared function to the tile intrinsic it names. Matching is by
// symbol and signature, so a user function that happens to share one of the
// kr32::matrix names keeps its own body and stays an ordinary call.
ir::Intrinsic matrix_intrinsic(const ir::Function& function) {
    if (!function.type || function.type->kind != ir::TypeKind::Function ||
        !function.declaration) {
        return ir::Intrinsic::None;
    }
    const auto& parameters = function.type->parameters;
    const auto& result = function.type->return_type;
    const auto& name = function.name;
    if (name == "matrix_mul") {
        if (parameters.size() == 2 && points_to_float_lane_record(parameters[0], 16) &&
            points_to_float_lane_record(parameters[1], 16) &&
            ir::is_float_lane_record(result, 16)) {
            return ir::Intrinsic::MatrixMultiply;
        }
    } else if (name == "matrix_mul_add") {
        if (parameters.size() == 3 && points_to_float_lane_record(parameters[0], 16) &&
            points_to_float_lane_record(parameters[1], 16) &&
            points_to_float_lane_record(parameters[2], 16) &&
            ir::is_float_lane_record(result, 16)) {
            return ir::Intrinsic::MatrixMultiplyAdd;
        }
    } else if (name == "transpose") {
        if (parameters.size() == 1 && points_to_float_lane_record(parameters[0], 16) &&
            ir::is_float_lane_record(result, 16)) {
            return ir::Intrinsic::MatrixTranspose;
        }
    } else if (name == "transform") {
        if (parameters.size() == 2 && points_to_float_lane_record(parameters[0], 16) &&
            points_to_float_lane_record(parameters[1], 4) &&
            ir::is_float_lane_record(result, 4)) {
            return ir::Intrinsic::MatrixVectorTransform;
        }
    } else if (name == "dot") {
        if (parameters.size() == 2 && points_to_float_lane_record(parameters[0], 4) &&
            points_to_float_lane_record(parameters[1], 4) && ir::is_float(result) &&
            result->bits == 32) {
            return ir::Intrinsic::VectorDot;
        }
    } else if (name == "push") {
        if (parameters.size() == 1 && points_to_float_lane_record(parameters[0], 16) &&
            ir::is_void(result)) {
            return ir::Intrinsic::TransformPush;
        }
    } else if (name == "pop") {
        if (parameters.size() == 1 && points_to_float_lane_record(parameters[0], 16) &&
            ir::is_void(result)) {
            return ir::Intrinsic::TransformPop;
        }
    } else if (name == "drop") {
        if (parameters.empty() && ir::is_void(result)) {
            return ir::Intrinsic::TransformDrop;
        }
    }
    return ir::Intrinsic::None;
}

// The symbol suffix for a class's metadata: `_ZTV<tag>`, `_ZTI<tag>`,
// `_ZTS<tag>`. A qualified tag flattens to underscores so the name is a valid
// assembler symbol.
std::string type_symbol(const frontend::RecordInfo& record, std::string_view prefix) {
    std::string name(prefix);
    name += record.tag;
    for (auto& character : name) {
        const auto uc = static_cast<unsigned char>(character);
        if (!std::isalnum(uc) && character != '_') {
            character = '_';
        }
    }
    return name;
}

std::string mangle_method(std::string_view tag, std::string_view name) {
    if (name.starts_with("operator")) {
        std::string suffix;
        for (const char character : name.substr(8)) {
            if (character == '+') {
                suffix += "add";
            } else if (character == '-') {
                suffix += "sub";
            } else if (character == '*') {
                suffix += "mul";
            } else if (character == '/') {
                suffix += "div";
            } else if (character == '=') {
                suffix += "eq";
            } else if (character == '<') {
                suffix += "lt";
            } else if (character == '>') {
                suffix += "gt";
            } else if (character == '(') {
                suffix += "call";
            } else if (character == '[') {
                suffix += "index";
            } else if ((character >= 'a' && character <= 'z') ||
                       (character >= 'A' && character <= 'Z') ||
                       (character >= '0' && character <= '9')) {
                suffix += character;
            }
        }
        return std::string(tag) + "_op_" + suffix;
    }
    if (name.starts_with("~")) {
        return std::string(tag) + "_dtor";
    }
    return std::string(tag) + "_" + std::string(name);
}

}  // namespace

CIRGen::CIRGen(frontend::Sema& sema, const frontend::AstNode& translation_unit,
               const target::TargetProfile& profile, frontend::DiagnosticEngine& diagnostics)
    : sema_(sema), unit_(translation_unit), profile_(profile), diagnostics_(diagnostics),
      module_(std::make_unique<ir::Module>(profile.id)), builder_(module_->types()) {}

std::unique_ptr<ir::Module> CIRGen::generate() {
    collect_declarations();
    clone_inherited_methods();
    for (const auto& [function, definition] : method_bodies_) {
        const auto class_found = function_classes_.find(function);
        if (class_found != function_classes_.end() && class_found->second) {
            const auto& record = *class_found->second;
            const bool pure =
                std::any_of(record.methods.begin(), record.methods.end(),
                            [](const frontend::RecordMethod& method) {
                                return method.is_pure;
                            });
            if (pure && function->name.find("_ctor_") == std::string::npos) {
                function->declaration = true;
                continue;
            }
        }
        generate_function(*definition, function);
    }
    for (const auto* definition : definitions_) {
        if (definition) {
            generate_function(*definition);
        }
    }
    for (const auto& [type, record] : ir_records_) {
        static_cast<void>(type);
        static_cast<void>(record);
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
    const auto record_type = record_types_.find(record);
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
        if (found != own_methods->second.end() &&
            !found->second->declaration &&
            !found->second->blocks.empty()) {
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
        builder_.create_cond_br(compare, body, done);
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
                builder_.create_cond_br(compare, body, done);
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
            emit_initializer(*info.expression, info.global,
                             info.global->element_type);
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
    if (record.base && record.base->kind == frontend::TypeKind::Record &&
        record.base->record) {
        const auto base_type = map_record(*record.base->record);
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
                                          false)) {
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
    const std::string mangled =
        is_constructor
            ? record_name + "_ctor_" + std::to_string(parameter_count) + "_value"
            : mangle_method(record_name, method_name);
    auto* function = module_->add_function(mangled, ir_type, ir::Linkage::Weak,
                                           false);
    function->debug_name =
        is_constructor ? record_name + "::" + class_name : record_name + "::" + method_name;
    function->add_parameter(module_->types().pointer_type(class_type), "this");
    const auto* function_declarator = find_function_declarator(*definition.children[1]);
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
    const bool is_incdec = method_name == "operator++" ||
                           method_name == "operator--";
    const auto method_key =
        is_constructor
            ? method_name + "#" + std::to_string(parameter_count) + "#value"
            : is_incdec
                  ? method_name + "#" + std::to_string(parameter_count)
                  : method_name;
    class_methods_[record_name][method_key] = function;
    if (is_constructor) {
        class_constructors_[record_name].push_back(function);
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
            if (method_name.empty() ||
                (!is_constructor && !is_assignment && !is_incdec &&
                 methods.contains(method_name))) {
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
                std::size_t declared_parameters = 0;
                for (const auto* cursor = member->children[1].get(); cursor != nullptr;) {
                    if (cursor->kind == frontend::AstKind::FunctionDeclarator) {
                        for (const auto& parameter : cursor->children) {
                            if (parameter &&
                                parameter->kind ==
                                    frontend::AstKind::ParameterDeclaration) {
                                ++declared_parameters;
                            }
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
                                : mangle_method(child->text, method_name);
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
                                : method_name;
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
            if (!fallback) {
                fallback = function;
            }
        }
        if (argument_count >= required && argument_count <= total) {
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
        builder_.create_cond_br(condition, then_block, else_block);

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
        builder_.create_cond_br(condition, body_block, end_block);
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
        builder_.create_cond_br(condition, body_block, end_block);
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
        builder_.create_cond_br(condition, body_block, end_block);

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
            builder_.create_cond_br(condition, body_block, end_block);
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
            builder_.create_cond_br(comparison, block, next);
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
        builder_.create_cond_br(compare, body, done);
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
                builder_.create_cond_br(condition, init_block, end_block);
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
                    builder_.create_cond_br(condition, init_block, end_block);
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
                        builder_.create_cond_br(condition, init_block,
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
                        builder_.create_cond_br(condition, init_block,
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
                    note_scope_cleanup();
                    local_cleanups_.push_back(
                        {address, destructor, type->count, type->element});
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
                    auto value = emit_expression(*init->children[1]);
                    if (value.value && value.type &&
                        value.type->kind == ir::TypeKind::Struct) {
                        auto* storage = builder_.create_alloca(
                            value.type, 0, "reference.temp");
                        builder_.create_store(value.value, storage);
                        target.value = storage;
                        target.type = value.type;
                    }
                } else {
                    target = emit_lvalue(*init->children[1]);
                }
                if (target.value) {
                    if (address->type && address->type->element &&
                        target.value->type != address->type->element) {
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
            type && type->kind == ir::TypeKind::Struct) {
            const auto record = ir_records_.find(type);
            ir::Function* constructor = nullptr;
            const auto& argument_nodes = init->children[1]->children;
            int reference_mode = 0;
            if (!argument_nodes.empty() && argument_nodes.front()) {
                if (argument_nodes.front()->kind == frontend::AstKind::Identifier) {
                    if (auto* local = lookup_local(argument_nodes.front()->text)) {
                        if (local->frontend_type &&
                            local->frontend_type->kind == frontend::TypeKind::Record) {
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
                if (argument_nodes.size() == 1 && argument_nodes.front()) {
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
                    note_scope_cleanup();
                    local_cleanups_.push_back({address, cleanup, 1, nullptr});
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
            if (value->type != current_function_->type->return_type) {
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
    builder_.create_cond_br(condition, then_block, else_block);

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
    const auto type = type_for_integer_literal(expression.text);
    return {module_->constant_int(type, value), type};
}

ir::TypePtr CIRGen::type_for_integer_literal(std::string_view text) {
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
    if (long_count >= 2) {
        return module_->types().int_type(profile_.long_long_bits, !is_unsigned);
    }
    if (long_count == 1) {
        return module_->types().int_type(profile_.long_bits, !is_unsigned);
    }
    return module_->types().int_type(profile_.int_bits, !is_unsigned);
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
    const bool left_names_object =
        left_kind == frontend::AstKind::MemberExpression ||
        (left_kind == frontend::AstKind::UnaryExpression &&
         expression.children[0]->text == "*");
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
        builder_.create_cond_br(left_bool, is_and ? right_block : merge_block,
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
            error(expression.location, "dereference of a non-pointer");
            return {};
        }
        const auto type = pointer.type->element;
        return {builder_.create_load(type, pointer.value, false, 0, "deref"), type};
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
                auto* element_pointer = builder_.create_gep(
                    type, pointer,
                    {module_->constant_int(size_type(),
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
                    builder_.create_cond_br(condition, body_block, end_block);
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
            builder_.create_cond_br(is_null, done_block, nonnull_block);
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
                                    static_cast<std::int64_t>(slot) * 4)},
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
                builder_.create_cond_br(condition, body_block, end_block);
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
                            static_cast<std::int64_t>(destructor_slot) * 4)},
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
            error(expression.location, "dereference of a non-pointer lvalue");
            return {};
        }
        return {pointer.value, pointer.type->element};
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
    if (expression.kind == frontend::AstKind::CallExpression) {
        // A reference-returning call names the object it refers to; any other
        // call has no address of its own, so callers that need one for the
        // value it returns get a temporary.
        auto value = emit_expression(expression);
        if (value.value && value.type) {
            auto* storage = builder_.create_alloca(value.type, 0, "call.temp");
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
    // The callee returned the address of a reference; a value context reads it.
    return {builder_.create_load(result_type->element, call, false, 0,
                                 "reference.result"),
            result_type->element};
}
CIRGen::Expression CIRGen::emit_call(const frontend::AstNode& expression) {
    if (expression.children.size() < 2 || !expression.children[0] ||
        !expression.children[1]) {
        return {};
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
        builder_.create_cond_br(condition, then_block, else_block);
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
        const auto named = sema_.type_named(expression.children[0]->text);
        if (named && named->kind == frontend::TypeKind::Record) {
            const auto type = map_type(named);
            const auto record = ir_records_.find(type);
            if (record != ir_records_.end() && record->second) {
                const auto& argument_nodes = expression.children[1]->children;
                auto* constructor = select_constructor(
                    record->second->tag, argument_nodes.size(), 0);
                if (!constructor) {
                    error(expression.location,
                          "constructor-initialized class has no constructor");
                    return {};
                }
                auto* storage =
                    builder_.create_alloca(type, 0, "temporary.record");
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
                            if (parameter_index <
                                method->second->type->parameters.size()) {
                                argument.value = promote(
                                    argument.value, argument.type,
                                    method->second->type
                                        ->parameters[parameter_index]);
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
                                    static_cast<std::int64_t>(slot) * 4)},
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
                base = emit_expression(*member.children.front());
                if (base.type && base.type->kind == ir::TypeKind::Pointer) {
                    record_type = base.type->element;
                }
            } else {
                if (member.children.front()->kind ==
                    frontend::AstKind::CallExpression) {
                    auto value = emit_expression(*member.children.front());
                    if (value.value && value.type &&
                        value.type->kind == ir::TypeKind::Struct) {
                        auto* storage = builder_.create_alloca(
                            value.type, 0, "this.temp");
                        builder_.create_store(value.value, storage);
                        base.value = storage;
                        base.type = value.type;
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
                const auto class_methods =
                    class_methods_.find(source_record->second->tag);
                if (class_methods != class_methods_.end()) {
                    const auto method = class_methods->second.find(member.text);
                    if (method != class_methods->second.end()) {
                        method_function = method->second;
                    }
                }
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
                        const auto kind = argument_nodes[index]->kind;
                        const bool addressable =
                            kind == frontend::AstKind::Identifier ||
                            kind == frontend::AstKind::MemberExpression ||
                            kind == frontend::AstKind::SubscriptExpression ||
                            (kind == frontend::AstKind::UnaryExpression &&
                             argument_nodes[index]->text == "*");
                        if (addressable) {
                            argument = emit_lvalue(*argument_nodes[index]);
                        }
                        if (!argument.value) {
                            auto value = emit_expression(*argument_nodes[index]);
                            if (value.value && value.type) {
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
                                size_type(), static_cast<std::int64_t>(slot) * 4)},
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
    for (std::size_t index = 0; index < argument_nodes.size(); ++index) {
        if (!argument_nodes[index]) {
            continue;
        }
        bool reference_parameter = false;
        if (direct_function) {
            const auto ast_type = function_ast_types_.find(direct_function);
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
                }
            } else {
                // Only an addressable argument is bound directly; an rvalue
                // bound to a const reference lives in a temporary.
                const auto kind = argument_nodes[index]->kind;
                const bool addressable =
                    kind == frontend::AstKind::Identifier ||
                    kind == frontend::AstKind::MemberExpression ||
                    kind == frontend::AstKind::SubscriptExpression ||
                    (kind == frontend::AstKind::UnaryExpression &&
                     argument_nodes[index]->text == "*");
                if (addressable) {
                    argument = emit_lvalue(*argument_nodes[index]);
                }
                if (!argument.value) {
                    auto value = emit_expression(*argument_nodes[index]);
                    if (value.value && value.type) {
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
        base = emit_expression(*expression.children.front());
        if (!base.value || !base.type || base.type->kind != ir::TypeKind::Pointer ||
            !base.type->element || base.type->element->kind != ir::TypeKind::Struct) {
            error(expression.location, "member access on a non-record pointer");
            return {};
        }
        record_type = base.type->element;
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
            auto value = emit_expression(*expression.children.front());
            if (value.value && value.type &&
                value.type->kind == ir::TypeKind::Struct) {
                auto* storage =
                    builder_.create_alloca(value.type, 0, "member.temp");
                builder_.create_store(value.value, storage);
                base.value = storage;
                base.type = value.type;
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
    auto* self_value =
        builder_.create_load(self->type, self->address, false, 0, "this.load");
    auto* byte_type = module_->types().int_type(8, false);
    auto* byte_pointer = builder_.create(
        ir::Opcode::BitCast, module_->types().pointer_type(byte_type), {self_value},
        "this.byte");
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
            return found->second->declaration ? nullptr : found->second;
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
    auto* functions = builder_.create_gep(
        byte_type, table_pointer, {module_->constant_int(int_type(), 4)}, false,
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
    builder_.create_cond_br(condition, body_block, end_block);
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
        if (!type || type->kind != frontend::TypeKind::Record || !type->record) {
            error(expression.location, "typeid of a non-class type is not implemented");
            return nullptr;
        }
        auto* global = ensure_typeinfo(*type->record);
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
            auto* info_slot = builder_.create_gep(
                byte_type, table, {module_->constant_int(int_type(), -4)}, false,
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
    create_call_instruction(throw_function,
                            {memory, typeinfo, module_->constant_null(byte_pointer)},
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
        builder_.create_cond_br(failed, throw_block, ok_block);
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
    create_call_instruction(throw_function,
                            {object, typeinfo, module_->constant_null(byte_pointer)},
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
        builder_.create_cond_br(condition, handler, next);
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
        std::uint64_t size = 0;
        std::uint64_t alignment = 1;
        for (const auto& field : type->fields) {
            const auto field_alignment = [&]() -> std::uint64_t {
                if (field->kind == ir::TypeKind::Array && field->element) {
                    return field->element->kind == ir::TypeKind::Float ? 8 : 4;
                }
                return field->kind == ir::TypeKind::Float ? 8 : 4;
            }();
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
