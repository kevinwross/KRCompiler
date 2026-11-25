#include "krc/frontend/sema.h"

#include <algorithm>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace krc::frontend {

namespace {

bool is_declarator_kind(AstKind kind) {
    return kind == AstKind::Declarator || kind == AstKind::PointerDeclarator ||
           kind == AstKind::ReferenceDeclarator ||
           kind == AstKind::ArrayDeclarator || kind == AstKind::FunctionDeclarator;
}

std::uint64_t round_up(std::uint64_t value, std::uint64_t alignment) {
    if (alignment <= 1) {
        return value;
    }
    return (value + alignment - 1) / alignment * alignment;
}

// A declarator nests its pieces, so a qualifier recorded on the function part
// is not visible on the outermost node.
// A record is abstract until every pure virtual member it inherits is overridden
// by a non-pure one; an object of such a type can never be created.
bool record_is_abstract(const RecordInfo& record) {
    std::vector<std::string> still_pure;
    for (const auto* level = &record; level != nullptr;
         level = level->base && level->base->kind == TypeKind::Record &&
                         level->base->record
                     ? level->base->record.get()
                     : nullptr) {
        for (const auto& method : level->methods) {
            if (method.is_pure &&
                std::find(still_pure.begin(), still_pure.end(), method.name) ==
                    still_pure.end()) {
                still_pure.push_back(method.name);
            }
        }
    }
    // A non-pure member anywhere in the chain overrides the pure one, whichever
    // level declared it.
    for (const auto* level = &record; level != nullptr;
         level = level->base && level->base->kind == TypeKind::Record &&
                         level->base->record
                     ? level->base->record.get()
                     : nullptr) {
        for (const auto& method : level->methods) {
            if (!method.is_pure) {
                const auto found =
                    std::find(still_pure.begin(), still_pure.end(), method.name);
                if (found != still_pure.end()) {
                    still_pure.erase(found);
                }
            }
        }
    }
    return !still_pure.empty();
}
bool declarator_has_label(const AstNode& declarator, const std::string& label) {
    if (std::find(declarator.labels.begin(), declarator.labels.end(), label) !=
        declarator.labels.end()) {
        return true;
    }
    for (const auto& child : declarator.children) {
        if (child && declarator_has_label(*child, label)) {
            return true;
        }
    }
    return false;
}

}  // namespace

Sema::Sema(DiagnosticEngine& diagnostics, const target::TargetProfile& profile,
           bool cxx_mode)
    : diagnostics_(diagnostics), profile_(profile), cxx_mode_(cxx_mode) {
    void_type_ = make_builtin(TypeKind::Void);
    bool_type_ = make_builtin(TypeKind::Bool, true);
    char_type_ = make_builtin(TypeKind::Char, profile_.char_unsigned);
    short_type_ = make_builtin(TypeKind::Short);
    int_type_ = make_builtin(TypeKind::Int);
    long_type_ = make_builtin(TypeKind::Long);
    long_long_type_ = make_builtin(TypeKind::LongLong);
    float_type_ = make_builtin(TypeKind::Float);
    double_type_ = make_builtin(TypeKind::Double);
    long_double_type_ = make_builtin(TypeKind::LongDouble);

    const auto size_kind = profile_.size_t_bits <= 16   ? TypeKind::Short
                           : profile_.size_t_bits <= 32 ? TypeKind::Int
                                                        : TypeKind::LongLong;
    size_type_ = make_builtin(size_kind, true);
    ptrdiff_type_ = make_builtin(size_kind);
    intmax_type_ = make_builtin(profile_.intmax_bits <= 32 ? TypeKind::Long
                                                           : TypeKind::LongLong);
}

Sema::~Sema() {
    // Record members can point back to their own record type. Break those
    // shared_ptr cycles once the Sema (and all types it owns) is being dropped.
    for (const auto& weak : record_infos_) {
        if (auto record = weak.lock()) {
            record->members.clear();
        }
    }
}

TypePtr Sema::make_builtin(TypeKind kind, bool is_unsigned) const {
    auto type = std::make_shared<Type>();
    type->kind = kind;
    type->is_unsigned = is_unsigned;
    return type;
}

TypePtr Sema::pointer_to(TypePtr element) const {
    auto type = std::make_shared<Type>();
    type->kind = TypeKind::Pointer;
    type->element = std::move(element);
    return type;
}

TypePtr Sema::array_of(TypePtr element, std::uint64_t count, bool has_count) const {
    auto type = std::make_shared<Type>();
    type->kind = TypeKind::Array;
    type->element = std::move(element);
    type->array_count = count;
    type->array_has_count = has_count;
    return type;
}

TypePtr Sema::function_of(TypePtr result, std::vector<TypePtr> parameters, bool variadic,
                          bool has_prototype) const {
    auto type = std::make_shared<Type>();
    type->kind = TypeKind::Function;
    type->return_type = std::move(result);
    type->parameters = std::move(parameters);
    type->variadic = variadic;
    type->has_prototype = has_prototype;
    return type;
}

TypePtr Sema::qualify(TypePtr type, bool is_const, bool is_volatile) const {
    if (!type) {
        return type;
    }
    auto qualified = std::make_shared<Type>(*type);
    qualified->is_const = qualified->is_const || is_const;
    qualified->is_volatile = qualified->is_volatile || is_volatile;
    return qualified;
}

TypePtr Sema::decay(TypePtr type) const {
    if (!type) {
        return type;
    }
    if (type->kind == TypeKind::Array && type->element) {
        return pointer_to(type->element);
    }
    if (type->kind == TypeKind::Function) {
        return pointer_to(type);
    }
    return type;
}

TypePtr Sema::promote(TypePtr type) const {
    if (!type) {
        return int_type_;
    }
    switch (type->kind) {
    case TypeKind::Bool:
    case TypeKind::Char:
    case TypeKind::Short:
    case TypeKind::Enum:
        return int_type_;
    default:
        return type;
    }
}

int Sema::rank(const Type& type) const {
    switch (type.kind) {
    case TypeKind::Bool:
        return 1;
    case TypeKind::Char:
        return 2;
    case TypeKind::Short:
        return 3;
    case TypeKind::Int:
    case TypeKind::Enum:
        return 4;
    case TypeKind::Long:
        return 5;
    case TypeKind::LongLong:
        return 6;
    case TypeKind::Float:
        return 7;
    case TypeKind::Double:
        return 8;
    case TypeKind::LongDouble:
        return 9;
    default:
        return 0;
    }
}

TypePtr Sema::usual_arithmetic(TypePtr left, TypePtr right) const {
    if (!left || !right) {
        return int_type_;
    }
    if (left->kind == TypeKind::LongDouble || right->kind == TypeKind::LongDouble) {
        return long_double_type_;
    }
    if (left->kind == TypeKind::Double || right->kind == TypeKind::Double) {
        return double_type_;
    }
    if (left->kind == TypeKind::Float || right->kind == TypeKind::Float) {
        return float_type_;
    }
    const auto left_type = promote(left);
    const auto right_type = promote(right);
    const int left_rank = rank(*left_type);
    const int right_rank = rank(*right_type);
    const bool use_unsigned = left_type->is_unsigned || right_type->is_unsigned;
    const int result_rank = std::max(left_rank, right_rank);
    switch (result_rank) {
    case 1:
    case 2:
    case 3:
    case 4:
        return make_builtin(TypeKind::Int, use_unsigned);
    case 5:
        return make_builtin(TypeKind::Long, use_unsigned);
    case 6:
        return make_builtin(TypeKind::LongLong, use_unsigned);
    default:
        return int_type_;
    }
}

void Sema::push_scope() { scopes_.emplace_back(); }

void Sema::pop_scope() {
    if (scopes_.size() > 1) {
        scopes_.pop_back();
    }
}

bool Sema::at_global_scope() const { return scopes_.size() == 1; }

Sema::Symbol* Sema::lookup_ordinary(std::string_view name) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        const auto found = scope->ordinary.find(std::string(name));
        if (found != scope->ordinary.end()) {
            return &found->second;
        }
    }
    const auto separator = name.rfind("::");
    if (separator != std::string_view::npos) {
        return lookup_ordinary(name.substr(separator + 2));
    }
    return nullptr;
}

Sema::Symbol* Sema::lookup_tag(std::string_view name) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        const auto found = scope->tags.find(std::string(name));
        if (found != scope->tags.end()) {
            return &found->second;
        }
    }
    return nullptr;
}

bool Sema::declare_ordinary(Symbol symbol) {
    auto& ordinary = scopes_.back().ordinary;
    const auto found = ordinary.find(symbol.name);
    if (found != ordinary.end()) {
        auto& previous = found->second;
        if (previous.kind == SymbolKind::Function && symbol.kind == SymbolKind::Function) {
            if (previous.defined && symbol.defined) {
                error(symbol.location, "redefinition of function '" + symbol.name + "'");
                return false;
            }
            if (symbol.defined) {
                previous.defined = true;
            }
            if (symbol.type) {
                previous.type = symbol.type;
            }
            return true;
        }
        if (previous.kind == SymbolKind::Variable && symbol.kind == SymbolKind::Variable &&
            at_global_scope()) {
            if (!previous.defined || !symbol.defined) {
                previous.defined = previous.defined || symbol.defined;
                if (symbol.type) {
                    previous.type = symbol.type;
                }
                return true;
            }
        }
        if (previous.kind == SymbolKind::Typedef && symbol.kind == SymbolKind::Typedef &&
            previous.type == symbol.type) {
            return true;
        }
        error(symbol.location, "redefinition of '" + symbol.name + "'");
        return false;
    }
    ordinary.emplace(symbol.name, std::move(symbol));
    return true;
}

bool Sema::declare_tag(Symbol symbol) {
    auto& tags = scopes_.back().tags;
    if (tags.find(symbol.name) != tags.end()) {
        error(symbol.location, "redefinition of tag '" + symbol.name + "'");
        return false;
    }
    tags.emplace(symbol.name, std::move(symbol));
    return true;
}

void Sema::error(const SourceLocation& location, std::string message) {
    had_error_ = true;
    diagnostics_.report(Severity::Error, location, std::move(message));
}

void Sema::warning(const SourceLocation& location, std::string message) {
    diagnostics_.report(Severity::Warning, location, std::move(message));
}

void Sema::check_member_access(const SourceLocation& location, const RecordInfo& record,
                               const std::string& name, AccessLevel access) {
    if (access == AccessLevel::Public) {
        return;
    }
    if (std::find(record.friends.begin(), record.friends.end(), current_function_name_) !=
        record.friends.end()) {
        return;
    }
    if (enclosing_record_ != nullptr) {
        // The defining class reaches its own members, and a class derived from
        // the declaring one reaches its protected members.
        if (enclosing_record_ == &record) {
            return;
        }
        if (std::find(record.friend_classes.begin(), record.friend_classes.end(),
                      enclosing_record_->tag) != record.friend_classes.end()) {
            return;
        }
        if (access == AccessLevel::Protected) {
            for (auto* base = enclosing_record_->base.get(); base != nullptr;) {
                const auto* base_record =
                    base->kind == TypeKind::Record ? base->record.get() : nullptr;
                if (base_record == &record) {
                    return;
                }
                base = base_record != nullptr ? base_record->base.get() : nullptr;
            }
        }
    }
    error(location, "'" + record.tag + "::" + name + "' is " +
                        (access == AccessLevel::Private ? "private" : "protected"));
}

TypePtr Sema::base_type_from_specifiers(const AstNode& specifiers, bool& is_typedef,
                                        bool& is_extern, bool& is_static) {
    is_typedef = false;
    is_extern = false;
    is_static = false;

    bool saw_void = false;
    bool saw_bool = false;
    bool saw_char = false;
    bool saw_short = false;
    bool saw_int = false;
    bool saw_float = false;
    bool saw_double = false;
    bool saw_signed = false;
    bool saw_unsigned = false;
    bool saw_complex = false;
    bool saw_imaginary = false;
    bool is_const = false;
    bool is_volatile = false;
    int long_count = 0;
    TypePtr typedef_type;
    TypePtr base;

    for (const auto& label : specifiers.labels) {
        if (label == "typedef") {
            is_typedef = true;
        } else if (label == "extern") {
            is_extern = true;
        } else if (label == "static") {
            is_static = true;
        } else if (label == "const") {
            is_const = true;
        } else if (label == "volatile") {
            is_volatile = true;
        } else if (label == "void") {
            saw_void = true;
        } else if (label == "_Bool" || label == "bool") {
            saw_bool = true;
        } else if (label == "char") {
            saw_char = true;
        } else if (label == "short") {
            saw_short = true;
        } else if (label == "int") {
            saw_int = true;
        } else if (label == "float") {
            saw_float = true;
        } else if (label == "double") {
            saw_double = true;
        } else if (label == "signed") {
            saw_signed = true;
        } else if (label == "unsigned") {
            saw_unsigned = true;
        } else if (label == "long") {
            ++long_count;
        } else if (label == "_Complex") {
            saw_complex = true;
        } else if (label == "_Imaginary") {
            saw_imaginary = true;
        } else if (label == "_Atomic") {
            error(specifiers.location, "_Atomic is not supported");
        } else if (label == "_Thread_local" || label == "thread_local") {
            // Thread storage duration is static storage on this single-threaded
            // VM. C++ spells the specifier thread_local and C11 spells it
            // _Thread_local; the declaration path gives a block-scope object
            // static storage and a guarded initializer, so the specifier only
            // has to be accepted here.
        } else if (label == "_Alignas" || label == "__inline" || label == "inline" ||
                   label == "_Noreturn") {
            // Qualifiers and function specifiers do not affect the value type here.
        } else if (const auto class_pattern = templates_.find(label);
                   class_pattern != templates_.end() &&
                   class_pattern->second.is_class &&
                   class_instances_.find(label) == class_instances_.end() &&
                   std::any_of(specifiers.labels.begin(), specifiers.labels.end(),
                               [](const std::string& argument_label) {
                                   return argument_label.rfind("template-arg:", 0) == 0;
                               })) {
            // Box<double> instantiates the class template; the pattern's own
            // record stands for the unparameterised spelling (ADR 0032).
            std::vector<std::string> arguments;
            for (const auto& argument_label : specifiers.labels) {
                constexpr std::string_view prefix = "template-arg:";
                if (argument_label.rfind(prefix, 0) == 0) {
                    arguments.push_back(argument_label.substr(prefix.size()));
                }
            }
            if (arguments.empty()) {
                continue;
            }
            arguments = expand_template_arguments(arguments);
            const auto instance = instantiate_class_template(label, arguments);
            if (!instance) {
                continue;
            }
            if (typedef_type) {
                error(specifiers.location, "multiple type names in declaration");
            }
            typedef_type = instance;
        } else if (const auto binding = template_bindings_.find(label);
                   binding != template_bindings_.end()) {
            // A template parameter shadows the file-scope name while an
            // instantiation is analyzed (ADR 0031).
            if (typedef_type) {
                error(specifiers.location, "multiple type names in declaration");
            }
            typedef_type = binding->second;
        } else if (auto* symbol = lookup_ordinary(label);
                   symbol != nullptr && symbol->kind == SymbolKind::Typedef) {
            if (typedef_type) {
                error(specifiers.location, "multiple typedef names in declaration");
            }
            typedef_type = symbol->type;
        }
    }

    for (const auto& child : specifiers.children) {
        if (!child) {
            continue;
        }
        if (child->kind == AstKind::StructSpecifier) {
            // A class template whose parameters include a value needs that
            // value while its own record is analysed; the stub the parser emits
            // names it, and the instance replaces it with a literal (ADR 0033).
            std::vector<std::string> value_stubs;
            for (const auto& label : child->labels) {
                constexpr std::string_view value_prefix = "template-value-param:";
                if (label.rfind(value_prefix, 0) == 0) {
                    const auto name = label.substr(value_prefix.size());
                    value_stubs.push_back(name);
                    template_value_bindings_.emplace(name, 1);
                }
            }
            base = record_type(*child, false);
            for (const auto& stub : value_stubs) {
                template_value_bindings_.erase(stub);
            }
            if ((cxx_mode_ ||
                 std::find(child->labels.begin(), child->labels.end(), "class") !=
                     child->labels.end()) &&
                !child->text.empty() && lookup_ordinary(child->text) == nullptr) {
                Symbol symbol;
                symbol.kind = SymbolKind::Typedef;
                symbol.name = child->text;
                symbol.type = base;
                symbol.location = child->location;
                declare_ordinary(std::move(symbol));
            }
        } else if (child->kind == AstKind::UnionSpecifier) {
            base = record_type(*child, true);
        } else if (child->kind == AstKind::EnumSpecifier) {
            base = enum_type(*child);
        } else if (child->kind == AstKind::TypeName) {
            base = analyze_type_name(*child);
        }
    }

    if (saw_complex) {
        error(specifiers.location, "_Complex is not supported");
    }
    if (saw_imaginary) {
        error(specifiers.location, "_Imaginary is not supported");
    }
    if (saw_signed && saw_unsigned) {
        error(specifiers.location, "both 'signed' and 'unsigned' in declaration");
    }
    if (saw_short && long_count > 0) {
        error(specifiers.location, "both 'short' and 'long' in declaration");
    }
    if (long_count > 2) {
        error(specifiers.location, "too many 'long' specifiers");
    }

    if (typedef_type) {
        if (!base) {
            base = typedef_type;
        }
    } else if (!base) {
        if (saw_void) {
            base = void_type_;
        } else if (saw_bool) {
            base = bool_type_;
        } else if (saw_float) {
            base = float_type_;
        } else if (saw_double) {
            base = long_count > 0 ? long_double_type_ : double_type_;
        } else if (saw_char) {
            base = make_builtin(TypeKind::Char,
                                saw_unsigned || (!saw_signed && profile_.char_unsigned));
        } else if (saw_short) {
            base = make_builtin(TypeKind::Short, saw_unsigned);
        } else if (long_count >= 2) {
            base = make_builtin(TypeKind::LongLong, saw_unsigned);
        } else if (long_count == 1) {
            base = make_builtin(TypeKind::Long, saw_unsigned);
        } else if (saw_int || saw_signed || saw_unsigned) {
            base = make_builtin(TypeKind::Int, saw_unsigned);
        }
    }

    if (!base) {
        if (specifiers.labels.empty() && specifiers.children.empty()) {
            return void_type_;
        }
        base = int_type_;
    }
    // plan.md §4.3: a type a profile cannot represent is diagnosed where it is
    // named, rather than lowered to something meaningless — `long long` became
    // a zero-width type and `float`/`double` reached the IR on a profile with
    // no FPU. A typedef carries the diagnostic from its own declaration.
    if (!typedef_type) {
        const char* unsupported = nullptr;
        if (base->kind == TypeKind::LongLong && profile_.long_long_bits == 0) {
            unsupported = "long long";
        } else if (base->kind == TypeKind::Float && profile_.float_bits == 0) {
            unsupported = "float";
        } else if (base->kind == TypeKind::Double && profile_.double_bits == 0) {
            unsupported = "double";
        } else if (base->kind == TypeKind::LongDouble && profile_.long_double_bits == 0) {
            unsupported = "long double";
        }
        if (unsupported != nullptr) {
            error(specifiers.location, std::string("'") + unsupported +
                                           "' is not supported by the " +
                                           std::string(profile_.name) + " profile");
        }
    }
    if (is_const || is_volatile) {
        base = qualify(base, is_const, is_volatile);
    }
    return base;
}

TypePtr Sema::record_type(const AstNode& specifier, bool is_union) {
    // A tagged specifier is a class template pattern: remember it, then analyse
    // the unparameterised record as before (ADR 0032).
    {
        std::vector<std::string> template_parameters;
        std::unordered_set<std::string> record_type_value_parameters;
        for (const auto& label : specifier.labels) {
            constexpr std::string_view prefix = "template-param:";
            constexpr std::string_view value_prefix = "template-value-param:";
            if (label.rfind(value_prefix, 0) == 0) {
                const auto name = label.substr(value_prefix.size());
                template_parameters.push_back(name);
                record_type_value_parameters.insert(name);
            } else if (label.rfind(prefix, 0) == 0) {
                template_parameters.push_back(label.substr(prefix.size()));
            }
        }
        const bool is_specialization =
            std::find(specifier.labels.begin(), specifier.labels.end(),
                      "specialization") != specifier.labels.end();
        if (!specifier.text.empty() && is_specialization) {
            // A partial specialization is registered apart from the primary: it is
            // a pattern that matches some argument lists, not a second definition
            // of the name. Its pack parameter is the one the parser labelled, and
            // every parameter before it is fixed.
            TemplateInfo info;
            info.parameters = template_parameters;
            info.definition = &specifier;
            info.name = specifier.text;
            info.is_class = true;
            info.value_parameters = std::move(record_type_value_parameters);
            specializations_[specifier.text].push_back(std::move(info));
        } else if (!template_parameters.empty() && !specifier.text.empty() &&
                   templates_.find(specifier.text) == templates_.end()) {
            TemplateInfo info;
            info.parameters = template_parameters;
            info.definition = &specifier;
            info.name = specifier.text;
            info.is_class = true;
            info.value_parameters = std::move(record_type_value_parameters);
            templates_.emplace(specifier.text, std::move(info));
        }
    }
    const auto& tag = specifier.text;
    std::shared_ptr<RecordInfo> info;
    TypePtr type;

    if (!tag.empty()) {
        if (auto* existing = lookup_tag(tag)) {
            if (!existing->type || existing->type->kind != TypeKind::Record) {
                error(specifier.location, "'" + tag + "' is not a struct or union tag");
                return int_type_;
            }
            type = existing->type;
            info = type->record;
            if (info && info->is_union != is_union) {
                error(specifier.location, "tag '" + tag + "' is used with a different kind");
            }
        } else {
            auto new_type = std::make_shared<Type>();
            new_type->kind = TypeKind::Record;
            info = std::make_shared<RecordInfo>();
            info->tag = tag;
            info->is_union = is_union;
            record_infos_.push_back(info);
            new_type->record = info;
            type = new_type;
            declare_tag(Symbol{SymbolKind::RecordTag, tag, type, specifier.location, false, 0});
            if (cxx_mode_ && lookup_ordinary(tag) == nullptr) {
                Symbol type_name;
                type_name.kind = SymbolKind::Typedef;
                type_name.name = tag;
                type_name.type = type;
                type_name.location = specifier.location;
                declare_ordinary(std::move(type_name));
            }
        }
    } else {
        auto new_type = std::make_shared<Type>();
        new_type->kind = TypeKind::Record;
        info = std::make_shared<RecordInfo>();
        info->is_union = is_union;
        record_infos_.push_back(info);
        new_type->record = info;
        type = new_type;
    }

    if (!info) {
        return type;
    }
    if (specifier.children.empty() &&
        std::find(specifier.labels.begin(), specifier.labels.end(),
                  "definition") == specifier.labels.end()) {
        return type;
    }
    if (info->complete) {
        // The same specifier is analyzed again when a typedef declaration
        // reaches its declarator; that is not a redefinition.
        if (info->definition != &specifier &&
            std::find(specifier.labels.begin(), specifier.labels.end(),
                      "specialization") == specifier.labels.end()) {
            error(specifier.location, "redefinition of " +
                                          std::string(is_union ? "union " : "struct ") + tag);
            return type;
        }
        return type;
    }

    info->definition = &specifier;
    info->complete = true;
    if (std::find(specifier.labels.begin(), specifier.labels.end(), "class") !=
        specifier.labels.end()) {
        info->default_access = AccessLevel::Private;
    }
    for (const auto& label : specifier.labels) {
        if (label == "final") {
            info->is_final = true;
        }
        if (label.starts_with("base:")) {
            info->base = type_named(label.substr(5));
            if (info->base && info->base->kind == TypeKind::Record &&
                info->base->record && info->base->record->is_final) {
                error(specifier.location,
                      "cannot derive from final class '" + info->base->record->tag +
                          "'");
            }
        }
    }
    // Bodies are analysed after every member is registered: a member body may
    // name a member declared later in the class.
    std::vector<const AstNode*> member_bodies;
    for (const auto& member : specifier.children) {
        if (!member) {
            continue;
        }
        if (member->kind == AstKind::FunctionDefinition &&
            member->children.size() >= 2 && member->children[0] && member->children[1]) {
            bool is_typedef = false;
            bool is_extern = false;
            bool is_static = false;
            auto result = base_type_from_specifiers(*member->children[0], is_typedef, is_extern,
                                                    is_static);
            auto function_type = build_declarator_type(*member->children[1], result);
            if (function_type && function_type->kind == TypeKind::Function) {
                const auto& labels = member->children[0]->labels;
                const bool is_virtual =
                    std::find(labels.begin(), labels.end(), "virtual") != labels.end();
                AccessLevel access = info->default_access;
                for (const auto& label : member->labels) {
                    if (label == "access:public") {
                        access = AccessLevel::Public;
                    } else if (label == "access:private") {
                        access = AccessLevel::Private;
                    } else if (label == "access:protected") {
                        access = AccessLevel::Protected;
                    }
                }
                RecordMethod method_entry;
                method_entry.name = declarator_name(*member->children[1]);
                method_entry.type = function_type;
                method_entry.is_virtual = is_virtual;
                method_entry.is_const =
                    declarator_has_label(*member->children[1], "const");
                method_entry.is_override =
                    declarator_has_label(*member->children[1], "override");
                method_entry.is_final =
                    declarator_has_label(*member->children[1], "final");
                method_entry.location = member->location;
                method_entry.access = access;
                info->methods.push_back(std::move(method_entry));
                member_bodies.push_back(member.get());
            }
            continue;
        }
        analyze_member_declaration(*member, *info);
    }
    // A class template's own record stands for the unparameterised spelling
    // (ADR 0032), so a body there is analysed with stub types: inside Holder<T>,
    // T* is int* and a body written for the real element fails on it. Only an
    // instance analyses its members' bodies, with the bindings that make them
    // valid.
    const bool pattern_only =
        !is_class_instance(info->tag) &&
        std::any_of(specifier.labels.begin(), specifier.labels.end(),
                    [](const std::string& label) {
                        return label.rfind("template-param:", 0) == 0 ||
                               label.rfind("template-value-param:", 0) == 0;
                    });
    if (pattern_only) {
        member_bodies.clear();
    }
    for (const auto* definition : member_bodies) {
        // An in-class body is analysed in the record's scope, like an
        // out-of-line one, once every member it may name exists.
        const auto saved_function_name = current_function_name_;
        const auto saved_enclosing_record = enclosing_record_;
        const auto* declarator = definition->children[1].get();
        current_function_name_ =
            info->tag + "::" + declarator_name(*declarator);
        enclosing_record_ = info.get();
        analyze_function_body(*definition, info.get());
        enclosing_record_ = saved_enclosing_record;
        current_function_name_ = saved_function_name;
    }
    // A derived member may not override one the base marked final, and an
    // `override` marker must name something the base actually declares.
    for (const auto& declared : info->methods) {
        bool found_in_base = false;
        bool base_marked_final = false;
        for (const auto* base =
                 info->base && info->base->kind == TypeKind::Record && info->base->record
                     ? info->base->record.get()
                     : nullptr;
             base != nullptr;
             base = base->base && base->base->record ? base->base->record.get() : nullptr) {
            for (const auto& inherited : base->methods) {
                if (inherited.name == declared.name) {
                    found_in_base = true;
                    base_marked_final = base_marked_final || inherited.is_final;
                }
            }
        }
        if (base_marked_final) {
            error(declared.location, "'" + info->tag + "::" + declared.name +
                                         "' overrides a final member");
        } else if (declared.is_override && !found_in_base) {
            error(declared.location,
                  "'" + info->tag + "::" + declared.name +
                      "' is marked override but overrides no base member");
        }
    }
    info->polymorphic =
        std::any_of(info->methods.begin(), info->methods.end(),
                    [](const RecordMethod& method) { return method.is_virtual; }) ||
        (info->base && info->base->kind == TypeKind::Record && info->base->record &&
         info->base->record->polymorphic);
    return type;
}

void Sema::analyze_member_declaration(const AstNode& member, RecordInfo& record) {
    if (member.children.empty()) {
        return;
    }
    const auto* specifiers = member.children.front().get();
    if (!specifiers) {
        return;
    }
    bool is_typedef = false;
    bool is_extern = false;
    bool is_static = false;
    auto base = base_type_from_specifiers(*specifiers, is_typedef, is_extern, is_static);

    AccessLevel access = record.default_access;
    for (const auto& label : member.labels) {
        if (label == "access:public") {
            access = AccessLevel::Public;
        } else if (label == "access:private") {
            access = AccessLevel::Private;
        } else if (label == "access:protected") {
            access = AccessLevel::Protected;
        }
    }
    const bool is_friend = std::find(member.labels.begin(), member.labels.end(), "friend") !=
                           member.labels.end();
    if (is_friend && member.children.size() == 1) {
        // `friend class X;` grants X access and declares no member. Without
        // this, the declaration reads as an anonymous member and copies X's
        // fields into this record.
        if (base && base->kind == TypeKind::Record && base->record &&
            !base->record->tag.empty()) {
            record.friend_classes.push_back(base->record->tag);
        }
        return;
    }

    std::uint64_t declaration_alignment = 0;
    for (const auto& specifier : specifiers->children) {
        if (!specifier || specifier->kind != AstKind::AlignasSpecifier ||
            specifier->children.empty() || !specifier->children.front()) {
            continue;
        }
        const auto* operand = specifier->children.front().get();
        long long alignment = 0;
        if (operand->kind == AstKind::TypeName) {
            alignment = static_cast<long long>(align_of(*analyze_type_name(*operand)));
        } else if (!evaluate_constant(*operand, alignment)) {
            error(specifier->location, "_Alignas requires an integer constant");
            continue;
        }
        if (alignment <= 0 || (alignment & (alignment - 1)) != 0) {
            error(specifier->location, "_Alignas requires a positive power of two");
            continue;
        }
        declaration_alignment = std::max(declaration_alignment,
                                         static_cast<std::uint64_t>(alignment));
    }

    if (member.children.size() == 1) {
        if (base && base->kind == TypeKind::Record && base->record && base->record->complete) {
            for (const auto& anonymous : base->record->members) {
                bool duplicate = false;
                for (const auto& existing : record.members) {
                    if (existing.name == anonymous.name) {
                        error(anonymous.location, "duplicate member '" + anonymous.name + "'");
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) {
                    record.members.push_back(anonymous);
                }
            }
        }
        return;
    }

    std::optional<unsigned> bit_width;
    for (std::size_t index = 1; index < member.children.size(); ++index) {
        const auto* child = member.children[index].get();
        if (!child) {
            continue;
        }
        if (child->kind == AstKind::DefaultMemberInitializer) {
            if (!record.members.empty() &&
                record.members.back().name == child->text) {
                record.members.back().default_initializer = child;
            }
            continue;
        }
        if (!is_declarator_kind(child->kind)) {
            long long width = 0;
            if (evaluate_constant(*child, width) && width >= 0) {
                bit_width = static_cast<unsigned>(width);
            }
            continue;
        }
        auto type = build_declarator_type(*child, base);
        const auto name = declarator_name(*child);
        if (name.empty()) {
            continue;
        }
        if (type->kind == TypeKind::Function) {
            bool is_virtual = false;
            for (const auto& label : member.children[0]->labels) {
                if (label == "virtual") {
                    is_virtual = true;
                }
            }
            const bool is_pure = std::find(member.labels.begin(), member.labels.end(),
                                           "pure") != member.labels.end();
            const bool is_deleted = std::find(member.labels.begin(), member.labels.end(),
                                              "deleted") != member.labels.end();
            // `const` is recorded on the declarator by the parser; the plain
            // `= delete` and pure markers travel on the member node.
            const bool is_const = declarator_has_label(*child, "const");
            const bool is_override = declarator_has_label(*child, "override");
            const bool is_final = declarator_has_label(*child, "final");
            if (is_friend) {
                // A friend declaration names a non-member function that may
                // reach this record's non-public members.
                record.friends.push_back(name);
                continue;
            }
            RecordMethod entry;
            entry.name = name;
            entry.type = type;
            entry.is_virtual = is_virtual;
            entry.is_pure = is_pure;
            entry.is_deleted = is_deleted;
            entry.is_const = is_const;
            entry.is_override = is_override;
            entry.is_final = is_final;
            entry.location = member.location;
            entry.access = access;
            // A constructor is registered under its own name plus its arity, so
            // a construction matches the constructor it would actually call.
            if (name == record.tag) {
                std::string arity_label = "constructor-arity:";
                std::string suffix;
                for (const auto& label : child->labels) {
                    if (label.rfind(arity_label, 0) == 0) {
                        suffix = "#" + label.substr(arity_label.size());
                        break;
                    }
                }
                // `explicit` decides whether a conversion may be implicit, so it
                // travels with the registered constructor's name.
                const bool is_explicit =
                    std::find(member.children[0]->labels.begin(),
                              member.children[0]->labels.end(),
                              "explicit") != member.children[0]->labels.end();
                entry.name = record.tag + suffix + (is_explicit ? ":explicit" : "");
            }
            record.methods.push_back(std::move(entry));
            continue;
        }
        if (is_static) {
            // A static data member lives at class scope whether or not it has
            // an in-class initializer, so a member body may name it. Only an
            // initialized one is a definition; the others are completed by an
            // out-of-class definition.
            bool has_initializer = false;
            for (const auto& member_child : member.children) {
                if (member_child &&
                    member_child->kind == AstKind::DefaultMemberInitializer) {
                    has_initializer = true;
                    break;
                }
            }
            scopes_.front().ordinary[record.tag + "::" + name] =
                Symbol{SymbolKind::Variable, record.tag + "::" + name, type,
                       child->location, has_initializer, 0};
            continue;
        }
        for (const auto& existing : record.members) {
            if (existing.name == name) {
                error(child->location, "duplicate member '" + name + "'");
            }
        }
        RecordMember member_info;
        member_info.name = name;
        member_info.type = std::move(type);
        member_info.location = child->location;
        member_info.alignment = declaration_alignment;
        member_info.access = access;
        // `mutable` travels on the declaration specifiers.
        const auto& specifier_labels = specifiers->labels;
        member_info.is_mutable =
            std::find(specifier_labels.begin(), specifier_labels.end(), "mutable") !=
            specifier_labels.end();
        if (bit_width) {
            member_info.bit_field = true;
            member_info.bit_width = *bit_width;
            bit_width.reset();
        }
        record.members.push_back(std::move(member_info));
    }
}

TypePtr Sema::enum_type(const AstNode& specifier) {
    const auto& tag = specifier.text;
    std::shared_ptr<EnumInfo> info;
    TypePtr type;

    if (!tag.empty()) {
        if (auto* existing = lookup_tag(tag)) {
            if (!existing->type || existing->type->kind != TypeKind::Enum) {
                error(specifier.location, "'" + tag + "' is not an enum tag");
                return int_type_;
            }
            type = existing->type;
            info = type->enum_info;
        } else {
            auto new_type = std::make_shared<Type>();
            new_type->kind = TypeKind::Enum;
            new_type->is_unsigned = false;
            info = std::make_shared<EnumInfo>();
            info->tag = tag;
            new_type->enum_info = info;
            type = new_type;
            declare_tag(Symbol{SymbolKind::EnumTag, tag, type, specifier.location, false, 0});
            if (cxx_mode_ && lookup_ordinary(tag) == nullptr) {
                Symbol type_name;
                type_name.kind = SymbolKind::Typedef;
                type_name.name = tag;
                type_name.type = type;
                type_name.location = specifier.location;
                declare_ordinary(std::move(type_name));
            }
        }
    } else {
        auto new_type = std::make_shared<Type>();
        new_type->kind = TypeKind::Enum;
        info = std::make_shared<EnumInfo>();
        new_type->enum_info = info;
        type = new_type;
    }

    if (!info || specifier.children.empty()) {
        return type;
    }
    if (info->complete) {
        if (info->definition != &specifier) {
            error(specifier.location, "redefinition of enum '" + tag + "'");
        }
        return type;
    }
    info->definition = &specifier;
    info->complete = true;
    long long next_value = 0;
    for (const auto& enumerator : specifier.children) {
        if (!enumerator || enumerator->kind != AstKind::Enumerator) {
            continue;
        }
        if (!enumerator->children.empty()) {
            long long value = 0;
            if (evaluate_constant(*enumerator->children.front(), value)) {
                next_value = value;
            } else {
                error(enumerator->location,
                      "enumerator value for '" + enumerator->text +
                          "' is not an integer constant");
            }
        }
        info->enumerators.emplace_back(enumerator->text, next_value);
        Symbol constant{SymbolKind::EnumConstant, enumerator->text, int_type_,
                        enumerator->location, true, next_value};
        declare_ordinary(std::move(constant));
        ++next_value;
    }
    return type;
}

void Sema::collect_declarator(const AstNode& declarator,
                              std::vector<const AstNode*>& operations,
                              std::string& name) const {
    if (declarator.kind == AstKind::Declarator) {
        name = declarator.text;
        return;
    }
    if (declarator.children.empty()) {
        return;
    }
    collect_declarator(*declarator.children.front(), operations, name);
    operations.push_back(&declarator);
}

std::string Sema::declarator_name(const AstNode& declarator) const {
    std::vector<const AstNode*> operations;
    std::string name;
    collect_declarator(declarator, operations, name);
    return name;
}

const AstNode* Sema::find_function_declarator(const AstNode& declarator) const {
    const AstNode* current = &declarator;
    while (current != nullptr) {
        if (current->kind == AstKind::FunctionDeclarator) {
            return current;
        }
        if (current->children.empty()) {
            return nullptr;
        }
        current = current->children.front().get();
    }
    return nullptr;
}

TypePtr Sema::parameter_type(const AstNode& parameter) {
    if (parameter.children.empty()) {
        return int_type_;
    }
    const auto* specifiers = parameter.children.front().get();
    if (!specifiers) {
        return int_type_;
    }
    bool is_typedef = false;
    bool is_extern = false;
    bool is_static = false;
    auto base = base_type_from_specifiers(*specifiers, is_typedef, is_extern, is_static);
    if (parameter.children.size() > 1) {
        if (const auto* declarator = parameter.children[1].get()) {
            base = build_declarator_type(*declarator, base);
        }
    }
    if (base && (base->kind == TypeKind::Array || base->kind == TypeKind::Function)) {
        base = decay(base);
    }
    return base;
}

TypePtr Sema::apply_declarator_operations(const std::vector<const AstNode*>& operations,
                                          TypePtr base) {
    TypePtr type = std::move(base);
    for (auto operation = operations.rbegin(); operation != operations.rend(); ++operation) {
        const auto& node = **operation;
        switch (node.kind) {
        case AstKind::PointerDeclarator:
            type = pointer_to(std::move(type));
            break;
        case AstKind::ReferenceDeclarator: {
            auto reference = std::make_shared<Type>(*pointer_to(std::move(type)));
            reference->is_reference = true;
            reference->is_rvalue_reference =
                std::find(node.labels.begin(), node.labels.end(), "rvalue") !=
                node.labels.end();
            type = std::move(reference);
            break;
        }
        case AstKind::ArrayDeclarator: {
            bool has_count = false;
            std::uint64_t count = 0;
            if (node.children.size() > 1) {
                long long value = 0;
                if (evaluate_constant(*node.children[1], value) && value >= 0) {
                    count = static_cast<std::uint64_t>(value);
                    has_count = true;
                } else {
                    error(node.location, "variable length arrays are not supported");
                }
            }
            type = array_of(std::move(type), count, has_count);
            break;
        }
        case AstKind::FunctionDeclarator: {
            std::vector<TypePtr> parameters;
            bool variadic = false;
            bool has_prototype = false;
            std::size_t default_count = 0;
            for (const auto& label : node.labels) {
                if (label == "variadic") {
                    variadic = true;
                } else if (label == "prototype") {
                    has_prototype = true;
                }
            }
            for (std::size_t index = 1; index < node.children.size(); ++index) {
                const auto* child = node.children[index].get();
                if (child && child->kind == AstKind::ParameterDeclaration) {
                    parameters.push_back(parameter_type(*child));
                    if (child->children.size() > 2 && child->children[2]) {
                        ++default_count;
                    }
                }
            }
            type = function_of(std::move(type), std::move(parameters), variadic,
                               has_prototype);
            if (type) {
                auto updated = std::make_shared<Type>(*type);
                updated->default_argument_count = default_count;
                type = std::move(updated);
            }
            break;
        }
        default:
            break;
        }
    }
    return type;
}

TypePtr Sema::build_declarator_type(const AstNode& declarator, TypePtr base) {
    std::vector<const AstNode*> operations;
    std::string name;
    collect_declarator(declarator, operations, name);
    return apply_declarator_operations(operations, std::move(base));
}

void Sema::break_cycles() {
    for (auto& scope : scopes_) {
        for (auto& [name, symbol] : scope.tags) {
            static_cast<void>(name);
            if (!symbol.type || !symbol.type->record) {
                continue;
            }
            for (auto& method : symbol.type->record->methods) {
                method.type.reset();
            }
        }
    }
}

void Sema::analyze(const AstNode& translation_unit) {
    scopes_.clear();
    push_scope();
    declare_ordinary(Symbol{SymbolKind::Function, "__builtin_va_start",
                            function_of(pointer_to(char_type_), {}, false, true), {}, false,
                            0});
    for (const auto& child : translation_unit.children) {
        if (!child) {
            continue;
        }
        switch (child->kind) {
        case AstKind::FunctionDefinition:
            analyze_function_definition(*child);
            break;
        case AstKind::Declaration:
            analyze_declaration(*child);
            break;
        case AstKind::StaticAssertDeclaration:
            analyze_static_assert(*child);
            break;
        default:
            analyze_statement(*child);
            break;
        }
    }
}

std::optional<Sema::GlobalSymbol> Sema::global_symbol(std::string_view name) const {
    if (scopes_.empty()) {
        return std::nullopt;
    }
    const auto found = scopes_.front().ordinary.find(std::string(name));
    if (found == scopes_.front().ordinary.end()) {
        return std::nullopt;
    }
    GlobalSymbol result;
    switch (found->second.kind) {
    case SymbolKind::Variable:
        result.kind = GlobalSymbol::Kind::Variable;
        break;
    case SymbolKind::Function:
        result.kind = GlobalSymbol::Kind::Function;
        break;
    case SymbolKind::Typedef:
        result.kind = GlobalSymbol::Kind::Typedef;
        break;
    case SymbolKind::EnumConstant:
    case SymbolKind::RecordTag:
    case SymbolKind::EnumTag:
        return std::nullopt;
    }
    result.type = found->second.type;
    result.defined = found->second.defined;
    return result;
}

namespace {

std::uint64_t string_literal_length(std::string_view text) {
    while (text.size() >= 2 && (text[0] == 'u' || text[0] == 'U' || text[0] == 'L')) {
        text.remove_prefix(text[0] == 'u' && text.size() >= 3 && text[1] == '8' ? 2 : 1);
    }
    if (text.size() < 2 || text.front() != '"') {
        return 0;
    }
    text.remove_prefix(1);
    if (!text.empty() && text.back() == '"') {
        text.remove_suffix(1);
    }
    std::uint64_t count = 0;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\\' && index + 1 < text.size()) {
            ++index;
        }
        ++count;
    }
    return count;
}

}  // namespace

TypePtr Sema::declaration_type(const AstNode& specifiers, const AstNode* declarator) {
    if (declarator != nullptr) {
        if (const auto recorded = declarator_types_.find(declarator);
            recorded != declarator_types_.end() && recorded->second) {
            return recorded->second;
        }
    }
    bool is_typedef = false;
    bool is_extern = false;
    bool is_static = false;
    auto base = base_type_from_specifiers(specifiers, is_typedef, is_extern, is_static);
    if (declarator) {
        base = build_declarator_type(*declarator, base);
    }
    return base;
}

TypePtr Sema::type_from_specifiers(const AstNode& specifiers) {
    bool is_typedef = false;
    bool is_extern = false;
    bool is_static = false;
    return base_type_from_specifiers(specifiers, is_typedef, is_extern, is_static);
}

TypePtr Sema::type_from_type_name_node(const AstNode& type_name) {
    return analyze_type_name(type_name);
}

std::uint64_t Sema::size_of_type(TypePtr type) const {
    return type ? size_of(*type) : 0;
}

std::uint64_t Sema::align_of_type(TypePtr type) const {
    return type ? align_of(*type) : 1;
}

bool Sema::evaluate_constant_expression(const AstNode& expression, long long& value) {
    if (const auto folded = constant_folds_.find(&expression);
        folded != constant_folds_.end()) {
        value = folded->second;
        return true;
    }
    return evaluate_constant(expression, value);
}

namespace {

// The name a template instantiation is mangled with: the source type name, so
// `max(1.5, 2.5)` becomes `max__double` (docs/abi.md, ADR 0031).
std::string template_type_tag(const TypePtr& type) {
    if (!type) {
        return "void";
    }
    switch (type->kind) {
    case TypeKind::Void:
        return "void";
    case TypeKind::Bool:
        return "bool";
    case TypeKind::Char:
        return type->is_unsigned ? "uchar" : "char";
    case TypeKind::Short:
        return type->is_unsigned ? "ushort" : "short";
    case TypeKind::Int:
        return type->is_unsigned ? "uint" : "int";
    case TypeKind::Long:
        return type->is_unsigned ? "ulong" : "long";
    case TypeKind::LongLong:
        return type->is_unsigned ? "ullong" : "llong";
    case TypeKind::Float:
        return "float";
    case TypeKind::Double:
        return "double";
    case TypeKind::LongDouble:
        return "longdouble";
    case TypeKind::Pointer:
        return std::string("ptr_") + template_type_tag(type->element);
    case TypeKind::Array:
        return std::string("array_") + template_type_tag(type->element);
    case TypeKind::Record:
        return type->record ? type->record->tag : "record";
    case TypeKind::Enum:
        return type->enum_info ? type->enum_info->tag : "enum";
    case TypeKind::Function:
        return "function";
    }
    return "unknown";
}

// True when the declarator wraps its named type in a pointer or reference.
bool declarator_has_indirection(const AstNode& declarator) {
    if (declarator.kind == AstKind::PointerDeclarator ||
        declarator.kind == AstKind::ReferenceDeclarator) {
        return true;
    }
    for (const auto& child : declarator.children) {
        if (child && declarator_has_indirection(*child)) {
            return true;
        }
    }
    return false;
}

// The declarator that names the function. The name is the front child chain
// of the function declarator, the same walk ind_function_declarator makes;
// descending into every child would reach a parameter's name instead.
const AstNode* function_name_declarator(const AstNode& declarator) {
    const AstNode* current = &declarator;
    while (current != nullptr) {
        if (current->kind == AstKind::Declarator) {
            return current;
        }
        if (current->children.empty()) {
            return nullptr;
        }
        current = current->children.front().get();
    }
    return nullptr;
}

}  // namespace

namespace {

// The position of a class template's pack parameter, or the parameter count when
// it has none. The parser records the pack as a label on the pattern's specifier,
// which lands on the definition or on its first child depending on the form, so
// both are read. A pattern's pack is always its last parameter in the ordering
// this slice supports, but the position is computed rather than assumed.
std::size_t pack_parameter_index(const std::vector<std::string>& parameters,
                                 const AstNode* definition) {
    if (definition == nullptr || parameters.empty()) {
        return parameters.size();
    }
    std::vector<std::string> labels = definition->labels;
    if (!definition->children.empty() && definition->children.front()) {
        for (const auto& label : definition->children.front()->labels) {
            labels.push_back(label);
        }
    }
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        const auto label = std::string("template-pack-param:") + parameters[index];
        if (std::find(labels.begin(), labels.end(), label) != labels.end()) {
            return index;
        }
    }
    return parameters.size();
}

// The mangled tag for an instantiated class: `Box__double` (ADR 0032).
std::string class_instance_tag(const std::string& tag,
                               const std::vector<std::string>& parameters,
                               const std::unordered_map<std::string, TypePtr>& bindings,
                               const std::unordered_map<std::string, long long>& values,
                               const std::unordered_map<std::string, std::size_t>& arities) {
    std::string name = tag;
    for (const auto& parameter : parameters) {
        name += "__";
        if (const auto value = values.find(parameter); value != values.end()) {
            name += std::to_string(value->second);
            continue;
        }
        const auto bound = bindings.find(parameter);
        name += bound == bindings.end() ? "unknown" : template_type_tag(bound->second);
        // A pack is identified by its arity: the same pattern at three arguments
        // and at two is two instances, and without the arity every instance of a
        // pack template collapses onto the first element's tag.
        const auto arity = arities.find(parameter);
        if (arity != arities.end()) {
            name += "__" + std::to_string(arity->second);
        }
    }
    return name;
}

// Reads an integer literal, decimal or 0x-prefixed hexadecimal.
bool parse_integer_text(const std::string& text, long long& value) {
    std::string trimmed;
    for (const auto character : text) {
        if (character != ' ' && character != '\t') {
            trimmed += character;
        }
    }
    if (trimmed.empty()) {
        return false;
    }
    std::size_t used = 0;
    try {
        const auto unsigned_text = trimmed[0] == '-' ? trimmed.substr(1) : trimmed;
        if (unsigned_text.rfind("0x", 0) == 0 || unsigned_text.rfind("0X", 0) == 0) {
            value = std::stoll(unsigned_text.substr(2), &used, 16);
            if (used + 2 != unsigned_text.size()) {
                return false;
            }
        } else {
            value = std::stoll(unsigned_text, &used, 10);
            if (used != unsigned_text.size()) {
                return false;
            }
        }
    } catch (const std::exception&) {
        return false;
    }
    if (trimmed[0] == '-') {
        value = -value;
    }
    return true;
}

// Replaces every reference to a value parameter with its number, so the
// instance's array bounds and expressions are literals (ADR 0033).
// Rewrites the injected class name of a pattern into the instance's tag, so
// `C* self()` inside `C<int>` means `C__int*` (ADR 0032).
void rewrite_instance_tag(AstNode& node, const std::string& from,
                          const std::string& to) {
    // A node that carries its own template arguments names a **different**
    // instantiation, so its class name is not the injected one. Renaming it too
    // turned `Tup<R...>` inside `Tup<int, int>` into `Tup__int__int__1` — the
    // record currently being built — whose layout then contains itself.
    const bool names_another_instantiation =
        std::any_of(node.labels.begin(), node.labels.end(),
                    [](const std::string& label) {
                        return label.rfind("template-arg:", 0) == 0;
                    });
    for (auto& label : node.labels) {
        if (!names_another_instantiation && label == from) {
            label = to;
        }
    }
    if (!names_another_instantiation && node.kind == AstKind::Identifier &&
        node.text == from) {
        node.text = to;
    }
    for (auto& child : node.children) {
        if (child) {
            rewrite_instance_tag(*child, from, to);
        }
    }
}
void substitute_value_parameters(
    AstNode& node, const std::unordered_map<std::string, long long>& values) {
    // A template argument is spelled as a label, so `array<T, N>` keeps `N` in a
    // label rather than in an expression: a value binding can satisfy it there
    // and a type lookup cannot, which is why the parameter's type stayed a stub.
    for (auto& label : node.labels) {
        constexpr std::string_view prefix = "template-arg:";
        if (label.rfind(prefix, 0) != 0) {
            continue;
        }
        const auto found = values.find(label.substr(prefix.size()));
        if (found != values.end()) {
            label = std::string(prefix) + std::to_string(found->second);
        }
    }
    for (auto& child : node.children) {
        if (!child) {
            continue;
        }
        if (child->kind == AstKind::Identifier) {
            const auto found = values.find(child->text);
            if (found != values.end()) {
                child->kind = AstKind::IntegerLiteral;
                child->text = std::to_string(found->second);
            }
        }
        substitute_value_parameters(*child, values);
    }
}

}  // namespace

// A class member whose signature carries the pack parameter needs two things done
// together, and doing only one of them is worse than doing neither.
//
// The signature must take one parameter per element: `tuple(T first, Rest... rest)`
// otherwise has a single parameter for the whole pack, so `tuple<int, char, long>`
// cannot be called with three arguments at all.
//
// The arguments that forward the pack must be rewritten to match, because the
// expansion named the parameters `rest`, `rest1`, ...: `tail(rest...)` has to become
// `tail(rest)` at arity one and `tail()` at arity zero. Rewriting the signature
// alone leaves `rest...` naming a parameter that no longer exists, which is the
// `use of undeclared identifier 'rest'` that made an earlier attempt look like a
// missing language feature.
//
// The arity is keyed by the *class* template's parameter spelling, which is what the
// member's pack specifier carries (`Rest`), not the parameter's own name (`rest`).
void Sema::expand_member_packs(
    AstNode& specifier,
    const std::unordered_map<std::string, std::size_t>& arities) {
    for (auto& member : specifier.children) {
        if (!member || member->kind != AstKind::FunctionDefinition ||
            member->children.size() < 2 || !member->children[1]) {
            continue;
        }
        auto* function =
            const_cast<AstNode*>(find_function_declarator(*member->children[1]));
        if (function == nullptr) {
            continue;
        }
        auto pack_position = function->children.size();
        auto spelling = std::string{};
        for (std::size_t index = 0; index < function->children.size(); ++index) {
            auto* parameter = function->children[index].get();
            if (parameter == nullptr ||
                parameter->kind != AstKind::ParameterDeclaration ||
                std::find(parameter->labels.begin(), parameter->labels.end(), "pack") ==
                    parameter->labels.end()) {
                continue;
            }
            pack_position = index;
            if (!parameter->children.empty() && parameter->children[0]) {
                for (const auto& label : parameter->children[0]->labels) {
                    if (label != "const" && label != "volatile") {
                        spelling = label;
                    }
                }
            }
            break;
        }
        const auto arity = arities.find(spelling);
        if (pack_position == function->children.size() || spelling.empty() ||
            arity == arities.end()) {
            continue;
        }
        auto* parameter = function->children[pack_position].get();
        parameter->labels.erase(
            std::remove(parameter->labels.begin(), parameter->labels.end(), "pack"),
            parameter->labels.end());
        const auto pack_name = parameter->children.size() > 1 && parameter->children[1]
                                   ? declarator_name(*parameter->children[1])
                                   : std::string{};
        if (pack_name.empty()) {
            continue;
        }
        // The arguments that forward the pack, rewritten before the signature grows
        // so a failure here leaves the member untouched.
        const auto expand_arguments = [&](auto& self,
                                          std::vector<std::unique_ptr<AstNode>>& nodes)
            -> void {
            for (std::size_t index = 0; index < nodes.size();) {
                auto& node = nodes[index];
                if (!node) {
                    ++index;
                    continue;
                }
                if (node->kind == AstKind::Identifier && node->text == pack_name &&
                    std::find(node->labels.begin(), node->labels.end(),
                              "pack-expansion") != node->labels.end()) {
                    std::vector<std::unique_ptr<AstNode>> expanded;
                    for (std::size_t element = 0; element < arity->second; ++element) {
                        auto copy = std::make_unique<AstNode>();
                        copy->kind = AstKind::Identifier;
                        copy->location = node->location;
                        copy->text = element == 0 ? pack_name
                                                  : pack_name + std::to_string(element);
                        expanded.push_back(std::move(copy));
                    }
                    nodes.erase(nodes.begin() + static_cast<std::ptrdiff_t>(index));
                    nodes.insert(nodes.begin() + static_cast<std::ptrdiff_t>(index),
                                 std::make_move_iterator(expanded.begin()),
                                 std::make_move_iterator(expanded.end()));
                    index += arity->second;
                    continue;
                }
                self(self, node->children);
                ++index;
            }
        };
        for (std::size_t index = 2; index < member->children.size(); ++index) {
            if (member->children[index]) {
                expand_arguments(expand_arguments, member->children[index]->children);
            }
        }
        // A pack that received nothing contributes no parameter at all.
        if (arity->second == 0) {
            function->children.erase(function->children.begin() +
                                     static_cast<std::ptrdiff_t>(pack_position));
            continue;
        }
        for (std::size_t copy = 1; copy < arity->second; ++copy) {
            auto duplicated = clone_ast(*parameter);
            if (duplicated->children.size() > 1 && duplicated->children[1]) {
                auto* declarator = const_cast<AstNode*>(duplicated->children[1].get());
                while (declarator != nullptr && declarator->kind != AstKind::Declarator) {
                    declarator = declarator->children.empty()
                                     ? nullptr
                                     : declarator->children.front().get();
                }
                if (declarator != nullptr) {
                    declarator->text = pack_name + std::to_string(copy);
                }
            }
            function->children.insert(function->children.begin() +
                                          static_cast<std::ptrdiff_t>(pack_position + copy),
                                      std::move(duplicated));
        }
    }
}

TypePtr Sema::instantiate_class_template(const std::string& tag,
                                         const std::vector<std::string>& arguments) {
    const auto pattern = templates_.find(tag);
    if (pattern == templates_.end() || !pattern->second.is_class ||
        !pattern->second.definition) {
        return nullptr;
    }
    // A registered partial specialization wins over the primary when its fixed
    // parameters fit the argument list: Tuple<T, Rest...> fixes one, so it takes
    // every arity of at least one and the primary keeps the empty case. That is the
    // whole ordering this slice supports -- argument count, not a partial order.
    const TemplateInfo* selected = &pattern->second;
    if (const auto found = specializations_.find(tag);
        found != specializations_.end()) {
        for (const auto& candidate : found->second) {
            if (candidate.parameters.empty()) {
                std::vector<std::string> spelled;
                if (candidate.definition) {
                    constexpr std::string_view prefix = "template-arg:";
                    for (const auto& label : candidate.definition->labels) {
                        if (label.rfind(prefix, 0) == 0) {
                            spelled.push_back(label.substr(prefix.size()));
                        }
                    }
                }
                if (!spelled.empty() && spelled == arguments) {
                    selected = &candidate;
                    break;
                }
                continue;
            }
            const auto pack =
                pack_parameter_index(candidate.parameters, candidate.definition);
            // The pack absorbs whatever the fixed parameters leave, which may be
            // nothing: `Tup<char>` matches `Tup<T, Rest...>` with an empty Rest, so
            // arity equal to the fixed count belongs to the specialization. Asking
            // for one argument per pack instead gave that arity to the primary, whose
            // members differ -- which is why `Tup<int, char>::tail` had no size.
            const bool matches =
                pack != candidate.parameters.size()
                    ? arguments.size() >= pack
                    : arguments.size() == candidate.parameters.size();
            if (matches) {
                selected = &candidate;
                break;
            }
        }
    }
    const TemplateInfo& pattern_info = *selected;
    std::unordered_map<std::string, TypePtr> bindings;
    std::unordered_map<std::string, long long> value_bindings;
    // A pack's arity, keyed by the pack's name, so the instance's signature can
    // take that many arguments and sizeof... can fold to it.
    std::unordered_map<std::string, std::size_t> pack_arities;
    // A pack parameter absorbs the tail of the argument list rather than matching
    // exactly one argument, which is what makes `Pack<int, char>` an instance
    // with arity 2 (ADR 0045). The parameter is identified by the label the parser
    // records on the pattern's specifier.
    const auto pack_index =
        pack_parameter_index(pattern_info.parameters, pattern_info.definition);
    if (!pattern_info.parameters.empty() &&
        pack_index == pattern_info.parameters.size()) {
        if (arguments.size() != pattern_info.parameters.size()) {
            return nullptr;
        }
    } else if (!pattern_info.parameters.empty() &&
               arguments.size() < pack_index) {
        // A pack may be empty, but the fixed parameters before it may not.
        return nullptr;
    }
    // The pack's arity is recorded before the arguments are walked, so a
    // zero-argument instance gets one: an empty pack is arity zero, and leaving it
    // unset made the instance tag say `unknown`, which is not a stable key -- the
    // same tag then described every empty instance, and a recursive record such as
    // tuple<> recursed forever looking for it.
    if (pack_index != pattern_info.parameters.size()) {
        pack_arities[pattern_info.parameters[pack_index]] =
            arguments.size() - pack_index;
    }
    // The instance's arguments in the order they were written, so a function
    // template whose signature names this class template can deduce its own
    // parameters from them. A value argument has no type and would misalign the
    // positions, so an instance built with one records nothing.
    std::vector<TypePtr> argument_types;
    bool has_value_argument = false;
    // A specialization binds no parameters: it names its arguments in its specifier and
    // its body is used verbatim. This loop walks the arguments and indexes the parameter
    // list at the same position, so entering it with no parameters indexed
    // `parameters[0]` out of range and crashed the compiler.
    for (std::size_t index = 0;
         index < arguments.size() && !pattern_info.parameters.empty(); ++index) {
        if (pack_index != pattern_info.parameters.size() && index >= pack_index) {
            // Every remaining argument belongs to the pack. Each element is bound
            // under its own name so a body can refer to them without expanding the
            // pack.
            const auto pack_parameter = pattern_info.parameters[pack_index];
            const auto element = template_argument_type(arguments[index]);
            if (!element) {
                return nullptr;
            }
            argument_types.push_back(element);
            bindings.emplace(pack_parameter + std::to_string(index - pack_index),
                             element);
            if (index == pack_index) {
                bindings.emplace(pack_parameter, element);
            }
            continue;
        }
        const auto parameter = pattern_info.parameters[index];
        if (pattern_info.value_parameters.count(parameter) != 0) {
            long long value = 0;
            if (!parse_integer_text(arguments[index], value)) {
                return nullptr;
            }
            value_bindings.emplace(parameter, value);
            has_value_argument = true;
            continue;
        }
        const auto bound = template_argument_type(arguments[index]);
        if (!bound) {
            return nullptr;
        }
        argument_types.push_back(bound);
        if (const auto existing = bindings.find(parameter); existing != bindings.end()) {
            if (existing->second != bound) {
                return nullptr;
            }
        } else {
            bindings.emplace(parameter, bound);
        }
    }

    // The cache is keyed by the instance's tag, not the spelled arguments: a
    // reference through a template parameter (Box<T>) and one through the
    // type (Box<int>) name the same instance.
    auto instance_tag =
        class_instance_tag(tag, pattern_info.parameters, bindings, value_bindings,
                          pack_arities);
    if (pattern_info.parameters.empty() && !arguments.empty()) {
        instance_tag = tag;
        for (const auto& argument : arguments) {
            instance_tag += "__" + argument;
        }
    }
    if (!has_value_argument) {
        class_instance_arguments_.emplace(instance_tag, argument_types);
    }
    if (const auto cached = class_instances_.find(instance_tag);
        cached != class_instances_.end()) {
        if (cached->second) {
            return cached->second;
        }
        // The instance is being built and one of its members refers to it: the
        // tag already names the record, so a reference resolves to the
        // incomplete type instead of starting a second instantiation.
        return type_named(instance_tag);
    }
    // The null entry marks the instance as in progress for that self-reference.
    class_instances_.emplace(instance_tag, nullptr);
    auto clone = clone_ast(*pattern_info.definition);
    clone->text = instance_tag;
    // Code generation collects a class's methods from its declaration
    // specifiers, so the instantiation is kept in a holder of that shape.
    // A value parameter becomes a literal in the instance, so its array bounds
    // and expressions are ordinary integer constants (ADR 0033).
    substitute_value_parameters(*clone, value_bindings);
    // The injected class name inside the pattern means the instance.
    for (auto& member : clone->children) {
        if (member) {
            rewrite_instance_tag(*member, tag, instance_tag);
        }
    }

    // A constructor or destructor is recognised by its class's name, so the
    // instance's members must carry the instance's tag (ADR 0032).
    for (auto& member : clone->children) {
        if (!member || (member->kind != AstKind::FunctionDefinition &&
                        member->kind != AstKind::MemberDeclaration) ||
            member->children.size() < 2 || !member->children[1]) {
            continue;
        }
        auto* named = const_cast<AstNode*>(
            function_name_declarator(*member->children[1]));
        if (!named) {
            continue;
        }
        if (named->text == tag) {
            named->text = instance_tag;
        } else if (named->text == "~" + tag) {
            named->text = "~" + instance_tag;
        }
    }
    // A member whose signature carries the pack takes one parameter per element, and
    // the arguments that forward the pack are rewritten to match.
    expand_member_packs(*clone, pack_arities);
    auto holder = make_ast(AstKind::DeclSpecifiers, clone->location);
    auto* specifier = clone.get();
    holder->children.push_back(std::move(clone));
    clone = std::move(holder);
    clone->labels.erase(
        std::remove_if(clone->labels.begin(), clone->labels.end(),
                       [](const std::string& label) {
                           return label.rfind("template-param:", 0) == 0 ||
                                  label.rfind("template-value-param:", 0) == 0 ||
                                  label.rfind("template-pack-param:", 0) == 0;
                       }),
        clone->labels.end());

    // The instance is analysed at file scope with only its own bindings active,
    // so the pattern's own names cannot leak into it and vice versa.
    auto saved_scopes = std::move(scopes_);
    scopes_.clear();
    scopes_.push_back(std::move(saved_scopes.front()));
    // A nested instantiation replaces both maps for its own analysis, and building
    // one record can instantiate another -- a `tail` member names a shorter tuple.
    // The enclosing instance's maps are restored afterwards so the rest of this
    // record is still analysed with its own bindings; clearing them instead left a
    // later member, or a member body, resolving `T` to its `int` stub.
    auto saved_bindings = std::move(template_bindings_);
    auto saved_pack_arities = std::move(template_pack_arities_);
    template_bindings_ = bindings;
    // sizeof...(pack) in a member body reads this map, so it must be live for the
    // duration of the instance's analysis (ADR 0045).
    template_pack_arities_ = pack_arities;
    auto type = record_type(*specifier, false);
    template_pack_arities_ = std::move(saved_pack_arities);
    template_bindings_ = std::move(saved_bindings);
    saved_scopes.front() = std::move(scopes_.front());
    scopes_ = std::move(saved_scopes);
    class_instantiations_.push_back(clone.get());
    class_instantiation_storage_.push_back(std::move(clone));
    if (type) {
        class_instances_.emplace(instance_tag, type);
    }
    return type;
}
std::vector<std::string> Sema::expand_template_arguments(
    const std::vector<std::string>& arguments) const {
    std::vector<std::string> expanded;
    for (const auto& argument : arguments) {
        constexpr std::string_view ellipsis = "...";
        if (argument.size() <= ellipsis.size() ||
            argument.compare(argument.size() - ellipsis.size(), ellipsis.size(),
                             ellipsis) != 0) {
            expanded.push_back(argument);
            continue;
        }
        const auto pack = argument.substr(0, argument.size() - ellipsis.size());
        const auto arity = template_pack_arities_.find(pack);
        if (arity == template_pack_arities_.end()) {
            expanded.push_back(argument);
            continue;
        }
        for (std::size_t index = 0; index < arity->second; ++index) {
            expanded.push_back(pack + std::to_string(index));
        }
    }
    return expanded;
}

bool Sema::deduce_class_template_arguments(
    const AstNode& specifier, const TypePtr& argument,
    const std::vector<std::string>& parameters,
    std::unordered_map<std::string, TypePtr>& bindings,
    std::unordered_map<std::string, std::size_t>& pack_arities) const {
    // The specifier's arguments, in the order they were written: `Tup<T, Rest...>`
    // spells two.
    std::vector<std::string> spelled;
    for (const auto& label : specifier.labels) {
        constexpr std::string_view prefix = "template-arg:";
        if (label.rfind(prefix, 0) == 0) {
            spelled.push_back(label.substr(prefix.size()));
        }
    }
    if (spelled.empty()) {
        return false;
    }
    // Every spelling must name a parameter of the function template. A concrete
    // type in the list (`Tup<int, Rest...>`) would have to be matched against the
    // instance's argument structurally, which this slice does not do; falling back
    // leaves the call behaving exactly as it did before this path existed.
    const auto names_parameter = [&](const std::string& name) {
        return std::find(parameters.begin(), parameters.end(), name) !=
               parameters.end();
    };
    for (const auto& spelling : spelled) {
        const auto pack = spelling.size() > 3 &&
                          spelling.compare(spelling.size() - 3, 3, "...") == 0;
        const auto name =
            pack ? spelling.substr(0, spelling.size() - 3) : spelling;
        if (!names_parameter(name)) {
            return false;
        }
    }
    // The record the argument names, unwrapped from a pointer, an array or a
    // reference. A reference parameter's argument arrives as the record itself,
    // which is why the pointer forms are only the first case here.
    auto type = argument;
    while (type && (type->kind == TypeKind::Pointer || type->kind == TypeKind::Array) &&
           type->element) {
        type = type->element;
    }
    if (!type || type->kind != TypeKind::Record || !type->record) {
        return false;
    }
    const auto supplied = class_instance_arguments_.find(type->record->tag);
    // The argument's record can be the class template's own pattern, which `tuple<>` is
    // and which the instance table does not hold because a pattern is not an instance.
    // The parameter's specifier names the template among its labels -- not in its text,
    // which is empty -- so matching a label against the record's tag identifies it. Every
    // spelling is already known to name a parameter of this template, and each is a pack,
    // so the pattern stands for the instance with no arguments.
    const bool pattern_as_empty_instance =
        supplied == class_instance_arguments_.end() &&
        class_instances_.find(type->record->tag) == class_instances_.end() &&
        std::any_of(specifier.labels.begin(), specifier.labels.end(),
                    [&](const std::string& label) {
                        return label == type->record->tag;
                    }) &&
        std::all_of(spelled.begin(), spelled.end(), [](const std::string& spelling) {
            return spelling.size() > 3 &&
                   spelling.compare(spelling.size() - 3, 3, "...") == 0;
        });
    if (supplied == class_instance_arguments_.end() && !pattern_as_empty_instance) {
        return false;
    }
    static const std::vector<TypePtr> no_arguments;
    const auto& arguments =
        pattern_as_empty_instance ? no_arguments : supplied->second;
    std::size_t position = 0;
    for (const auto& spelling : spelled) {
        const auto pack = spelling.size() > 3 &&
                          spelling.compare(spelling.size() - 3, 3, "...") == 0;
        if (pack) {
            // The pack takes every argument the fixed spellings before it did not,
            // including none at all. Each element is bound under the name a class
            // instance uses for it, which is the name a template-id in the body
            // expands to (Rest0, Rest1, ...).
            const auto name = spelling.substr(0, spelling.size() - 3);
            for (std::size_t index = position; index < arguments.size(); ++index) {
                bindings[name + std::to_string(index - position)] = arguments[index];
            }
            if (position < arguments.size()) {
                bindings[name] = arguments[position];
            }
            pack_arities[name] = arguments.size() - position;
            position = arguments.size();
            continue;
        }
        if (position >= arguments.size()) {
            return false;
        }
        bindings[spelling] = arguments[position];
        ++position;
    }
    return true;
}

TypePtr Sema::template_argument_type(const std::string& spelling) const {
    // A pack expansion reaches this point only when there was no instance pack to
    // expand it: a member written `Tup<Rest...>` is expanded into the pack's element
    // names before its arguments are resolved. Answering with the `int` stub instead
    // made the member's type the enclosing instance's first argument, and inside a
    // recursive template that names the record being built.
    if (spelling.find("...") != std::string::npos) {
        return nullptr;
    }
    // The spelling is turned back into specifier labels so the one place that
    // maps labels to types stays authoritative.
    auto specifiers = make_ast(AstKind::DeclSpecifiers, SourceLocation{});
    std::string word;
    for (const auto character : spelling) {
        if (character == ' ') {
            if (!word.empty()) {
                specifiers->labels.push_back(word);
                word.clear();
            }
            continue;
        }
        word += character;
    }
    if (!word.empty()) {
        specifiers->labels.push_back(word);
    }
    if (specifiers->labels.empty()) {
        return nullptr;
    }
    return const_cast<Sema*>(this)->type_from_specifiers(*specifiers);
}

TypePtr Sema::type_of_name(const AstNode& node) {
    // A name node that carries `template-arg:` labels names a template-id, as a
    // constructor call `Box<T>(value)` does. Resolving it by spelling would answer
    // with the unparameterised record, so the instantiation is asked instead --
    // the same call `type_from_specifiers` makes when the specifier carries the
    // labels. Without this the instance's body is emitted against the pattern.
    std::vector<std::string> arguments;
    const std::string prefix = "template-arg:";
    for (const auto& label : node.labels) {
        if (label.rfind(prefix, 0) == 0) {
            arguments.push_back(label.substr(prefix.size()));
        }
    }
    if (arguments.empty()) {
        return type_named(node.text);
    }
    arguments = expand_template_arguments(arguments);
    // The pattern is registered under the unqualified name, so a qualified template-id
    // must be instantiated by its last component. Passing the qualified spelling found
    // no pattern, fell back to the unparameterised record below, and code generation
    // then emitted the pattern's constructor for `std::tuple<int, int>(3, 4)`.
    std::string pattern_name = node.text;
    if (const auto separator = pattern_name.rfind("::");
        separator != std::string::npos) {
        pattern_name = pattern_name.substr(separator + 2);
    }
    if (const auto instance = instantiate_class_template(pattern_name, arguments)) {
        return instance;
    }
    return type_named(node.text);
}

namespace {

// Rewrites a fold node in place into a left-associative chain over the pack's
// expanded parameters: `(v + ...)` with three elements becomes (v + v1) + v2.
// The parameters are named `v`, `v1`, ... by the signature expansion, which is
// why the arity is needed here rather than only in Sema's fold for sizeof....
void fold_over_pack(AstNode& node, const std::string& name, std::size_t arity,
                    const std::string& operation) {
    const auto element = [&](std::size_t index) {
        auto identifier = std::make_unique<AstNode>();
        identifier->kind = AstKind::Identifier;
        identifier->location = node.location;
        identifier->text =
            index == 0 ? name : name + std::to_string(index);
        return identifier;
    };
    if (arity == 0) {
        node.kind = AstKind::IntegerLiteral;
        node.text = "0";
        node.children.clear();
        return;
    }
    if (arity == 1) {
        node.kind = AstKind::Identifier;
        node.text = name;
        node.children.clear();
        return;
    }
    auto left = std::make_unique<AstNode>();
    left->kind = AstKind::BinaryExpression;
    left->location = node.location;
    left->text = operation;
    left->children.push_back(element(0));
    left->children.push_back(element(1));
    for (std::size_t index = 2; index < arity; ++index) {
        auto combined = std::make_unique<AstNode>();
        combined->kind = AstKind::BinaryExpression;
        combined->location = node.location;
        combined->text = operation;
        combined->children.push_back(std::move(left));
        combined->children.push_back(element(index));
        left = std::move(combined);
    }
    auto first = std::move(left->children[0]);
    auto second = std::move(left->children[1]);
    node.children.clear();
    node.kind = AstKind::BinaryExpression;
    node.text = operation;
    node.children.push_back(std::move(first));
    node.children.push_back(std::move(second));
}

}  // namespace
std::string Sema::instantiate_template_call(
    const AstNode& callee, const std::vector<std::string>& explicit_arguments,
    const std::vector<TypePtr>& arguments) {
    // Templates are registered under the name their declaration spells, so a
    // call through a namespace — `std::make_unique(...)` — has to fall back to the
    // unqualified tail. Without that, a template in a namespace is never found and
    // the call targets a symbol nothing defines.
    auto pattern = templates_.find(callee.text);
    if (pattern == templates_.end()) {
        const auto separator = callee.text.rfind("::");
        if (separator != std::string::npos) {
            pattern = templates_.find(callee.text.substr(separator + 2));
        }
    }
    if (pattern == templates_.end() || !pattern->second.definition ||
        pattern->second.definition->children.size() < 2 ||
        !pattern->second.definition->children[1]) {
        return {};
    }
    // The parameters hang off the FunctionDeclarator, which may sit under a
    // PointerDeclarator when the return type has indirection: `T* construct(...)`
    // nests the function declarator one level down, and reading the outer node's
    // children yields the function declarator itself rather than the parameters.
    // That made the deduction loop see a single non-parameter child, so a pack in
    // such a signature was never considered and its type stayed unbound.
    const auto* pattern_function =
        find_function_declarator(*pattern->second.definition->children[1]);
    const auto& parameters =
        pattern_function ? pattern_function->children
                         : pattern->second.definition->children[1]->children;
    std::unordered_map<std::string, TypePtr> bindings;
    std::unordered_map<std::string, long long> value_bindings;
    // A value parameter is not reached by the positional deduction loop below, which
    // returns early on a parameter whose type is a template-id. Deduce it here instead,
    // from the argument whose parameter type names it: for `int f(Fixed<N>&)` called with
    // `Fixed<2>`, the argument's type records the instance tag `Fixed__2`, so the value is
    // the integer after the pattern's name. The parameter list carries the function's own
    // name at index 0, so the parameter at position p takes argument p - 1.
    for (std::size_t index = 0; index < pattern->second.parameters.size(); ++index) {
        const auto& name = pattern->second.parameters[index];
        if (pattern->second.value_parameters.count(name) == 0) {
            continue;
        }
        if (index < explicit_arguments.size()) {
            continue;
        }
        for (std::size_t position = 1; position < parameters.size(); ++position) {
            const AstNode* specifier =
                parameters[position] && !parameters[position]->children.empty()
                    ? parameters[position]->children[0].get()
                    : nullptr;
            if (specifier == nullptr) {
                continue;
            }
            std::string pattern_name;
            bool names_this_parameter = false;
            // `<string_view>` is included; `<cstring>` is not, and relying on a transitive
            // include here broke the GCC and Clang builds while MSVC compiled.
            constexpr std::string_view template_arg_label{"template-arg:"};
            for (const auto& label : specifier->labels) {
                if (label == "template-id") {
                    continue;
                }
                if (label.rfind(template_arg_label, 0) == 0) {
                    if (label.substr(template_arg_label.size()) == name) {
                        names_this_parameter = true;
                    }
                    continue;
                }
                if (pattern_name.empty()) {
                    pattern_name = label;
                }
            }
            if (!names_this_parameter || pattern_name.empty()) {
                continue;
            }
            const auto argument_index = position - 1;
            if (argument_index >= arguments.size() || !arguments[argument_index]) {
                continue;
            }
            const auto& argument = arguments[argument_index];
            std::shared_ptr<RecordInfo> record;
            if (argument->record) {
                record = argument->record;
            } else if (argument->element && argument->element->record) {
                record = argument->element->record;
            }
            if (!record) {
                continue;
            }
            const std::string prefix = pattern_name + "__";
            if (record->tag.rfind(prefix, 0) != 0) {
                continue;
            }
            const auto digits = record->tag.substr(prefix.size());
            if (digits.empty() ||
                digits.find_first_not_of("0123456789") != std::string::npos) {
                continue;
            }
            value_bindings.emplace(name, std::stoll(digits));
            break;
        }
        if (value_bindings.count(name) == 0) {
            error(callee.location,
                  "cannot deduce the value template parameter '" + name +
                      "'; write it explicitly");
            return {};
        }
    }
    // A pack's arity, keyed by the pack's name, so the instance's signature can
    // take that many arguments and sizeof... can fold to it.
    std::unordered_map<std::string, std::size_t> pack_arities;
    // An explicit argument list overrides deduction: parameter i is bound to
    // the spelled type, and only the rest are deduced from the call.
    std::unordered_set<std::string> explicit_bound;
    for (std::size_t index = 0;
         index < explicit_arguments.size() &&
         index < pattern->second.parameters.size();
         ++index) {
        const auto& parameter_name = pattern->second.parameters[index];
        if (pattern->second.value_parameters.count(parameter_name) != 0) {
            // A number, not a type: constant<3> binds N to 3 and the body is
            // emitted with the literal in place (ADR 0033).
            const auto& spelled = explicit_arguments[index];
            if (spelled.empty() ||
                spelled.find_first_not_of("0123456789") != std::string::npos) {
                return {};
            }
            value_bindings.emplace(parameter_name, std::stoll(spelled));
            continue;
        }
        const auto bound = template_argument_type(explicit_arguments[index]);
        if (!bound) {
            return {};
        }
        bindings.emplace(parameter_name, bound);
        explicit_bound.insert(parameter_name);
    }
    std::size_t argument_index = 0;
    for (std::size_t index = 1; index < parameters.size(); ++index) {
        const auto* parameter = parameters[index].get();
        if (!parameter || parameter->kind != AstKind::ParameterDeclaration ||
            parameter->children.size() < 2 || !parameter->children[0]) {
            continue;
        }
        // A pack is considered even when no argument is left: that is the
        // zero-arity instance, not a missing argument.
        const bool pack_head =
            std::find(parameter->labels.begin(), parameter->labels.end(), "pack") !=
            parameter->labels.end();
        if (!pack_head && argument_index >= arguments.size()) {
            continue;
        }
        const auto& labels = parameter->children[0]->labels;
        const auto template_parameter =
            std::find_if(labels.begin(), labels.end(), [&](const std::string& label) {
                return std::find(pattern->second.parameters.begin(),
                                 pattern->second.parameters.end(),
                                 label) != pattern->second.parameters.end();
            });
        if (template_parameter == labels.end()) {
            // A parameter whose type is a class template-id spells its arguments as
            // labels rather than naming a template parameter directly, so the search
            // above finds nothing: `arity_of(Tup<T, Rest...>&)`. Deduce from the
            // instance's recorded argument list. An explicitly spelled argument is
            // left alone: binding it here as well would silently overrule the caller.
            const bool explicitly_spelled =
                std::any_of(labels.begin(), labels.end(), [&](const std::string& label) {
                    constexpr std::string_view prefix = "template-arg:";
                    return label.rfind(prefix, 0) == 0 &&
                           explicit_bound.count(label.substr(prefix.size())) != 0;
                });
            if (!explicitly_spelled &&
                deduce_class_template_arguments(*parameter->children[0],
                                                arguments[argument_index],
                                                pattern->second.parameters, bindings,
                                                pack_arities)) {
                argument_index++;
                continue;
            }
            argument_index++;
            continue;
        }
        if (explicit_bound.count(*template_parameter) != 0) {
            argument_index++;
            continue;
        }
        const bool is_pack = pack_head;
        if (is_pack) {
            // Every remaining argument belongs to the pack. Its arity becomes part
            // of the instance, and the element type is the arguments' shared one:
            // a pack of mixed types needs a per-position binding this does not
            // have, so it is refused rather than mistyped.
            // A pack that received no argument is still a pack: its arity is
            // zero, which the signature expansion removes entirely. Returning
            // here would leave the call targeting the pattern.
            if (argument_index >= arguments.size()) {
                pack_arities.emplace(*template_parameter, std::size_t{0});
                if (parameter->children.size() > 1 && parameter->children[1]) {
                    const auto empty_name =
                        declarator_name(*parameter->children[1]);
                    if (!empty_name.empty()) {
                        pack_arities.emplace(empty_name, std::size_t{0});
                    }
                }
                break;
            }
            if (!arguments[argument_index]) {
                return {};
            }
            const auto element = arguments[argument_index];
            // Type pointers are not shared between two occurrences of int, so
            // the comparison is structural: kind, signedness, and the element for
            // the kinds that carry one.
            const auto same_shape = [](const TypePtr& left, const TypePtr& right,
                                     const auto& self) -> bool {
                if (!left || !right) {
                    return left == right;
                }
                if (left->kind != right->kind ||
                    left->is_unsigned != right->is_unsigned) {
                    return false;
                }
                if (left->kind == TypeKind::Pointer ||
                    left->kind == TypeKind::Array) {
                    return self(left->element, right->element, self);
                }
                if (left->kind == TypeKind::Record) {
                    return left->record && right->record &&
                           left->record->tag == right->record->tag;
                }
                return true;
            };
            // A pack whose elements differ is allowed: the instance name carries each
            // element's type, so `{int, int}` and `{int, char}` are two instances rather
            // than one. Before that, enabling this made the second call site store a
            // `tuple<int, int>` into a `tuple<int, char>`, so the refusal was right until
            // the name distinguished them.
            (void)same_shape;
            bindings.emplace(*template_parameter, element);
            const auto arity = arguments.size() - argument_index;
            pack_arities.emplace(*template_parameter, arity);
            for (std::size_t position = 0; position < arity; ++position) {
                bindings.emplace(*template_parameter + std::to_string(position),
                                 arguments[argument_index + position]);
            }
            if (parameter->children.size() > 1 && parameter->children[1]) {
                const auto parameter_name = declarator_name(*parameter->children[1]);
                if (!parameter_name.empty()) {
                    pack_arities.emplace(parameter_name, arity);
                }
            }
            argument_index = arguments.size();
            break;
        }
        TypePtr bound = arguments[argument_index];
        if (!bound) {
            return {};
        }
        if (declarator_has_indirection(*parameter->children[1])) {
            // `T*` and `T&` deduce from a pointer, a reference, or an array
            // that decays to a pointer; a value argument cannot deduce either.
            if (bound->kind == TypeKind::Array && bound->element) {
                bound = bound->element;
            } else if (bound->kind == TypeKind::Pointer && bound->element) {
                bound = bound->element;
            } else {
                return {};
            }
        }
        // A by-value `const T` still deduces T without the qualifier.
        const auto existing = bindings.find(*template_parameter);
        if (existing != bindings.end()) {
            if (existing->second != bound) {
                return {};
            }
        } else {
            bindings.emplace(*template_parameter, bound);
        }
        argument_index++;
    }
    if (bindings.empty() && value_bindings.empty() && pack_arities.empty()) {
        return {};
    }
    // A value parameter is not deduced from an argument's type, so a call that
    // omits it cannot pick an instance. Say so at the call rather than leaving a
    // symbol nothing defines.
    for (const auto& name : pattern->second.value_parameters) {
        if (value_bindings.count(name) == 0) {
            error(callee.location,
                  "cannot deduce the value template parameter '" + name +
                      "'; write it explicitly");
            return {};
        }
    }
    // The parser instantiates the all-int case as the definition itself, so a
    // call that deduces only `int` needs no clone.
    // The all-int shortcut assumes the pattern itself is the int instance. A
    // value parameter makes the pattern uncallable, so there is nothing to
    // reuse and the instance must be built.
    if (value_bindings.empty() && pack_arities.empty() &&
        bindings.size() == 1 &&
        bindings.begin()->second->kind == TypeKind::Int &&
        !bindings.begin()->second->is_unsigned) {
        return {};
    }
    std::string mangled = pattern->second.name;
    for (const auto& name : pattern->second.parameters) {
        // A pack contributes every element's type and its arity, and nothing else. Its
        // own binding is the first element, so appending that as well produced
        // `tuple_head__int__uchar__uchar__1` for a one-element pack: the accessor was
        // instantiated under a name its call sites never asked for. The separator is
        // emitted per piece here rather than once up front, because a pack emits none of
        // its own.
        if (const auto arity = pack_arities.find(name); arity != pack_arities.end()) {
            for (std::size_t index = 0; index < arity->second; ++index) {
                const auto element = bindings.find(name + std::to_string(index));
                mangled += "__";
                mangled += element == bindings.end()
                               ? "unknown"
                               : template_type_tag(element->second);
            }
            mangled += "__" + std::to_string(arity->second);
            continue;
        }
        mangled += "__";
        const auto value_binding = value_bindings.find(name);
        if (value_binding != value_bindings.end()) {
            mangled += std::to_string(value_binding->second);
            continue;
        }
        const auto binding = bindings.find(name);
        mangled += binding == bindings.end() ? "unknown"
                                             : template_type_tag(binding->second);
    }
    if (instantiated_names_.count(mangled) != 0) {
        return mangled;
    }

    auto clone = clone_ast(*pattern->second.definition);
    clone->labels.erase(
        std::remove_if(clone->labels.begin(), clone->labels.end(),
                       [](const std::string& label) {
                           return label.rfind("template-param:", 0) == 0 ||
                                  label.rfind("template-value-param:", 0) == 0 ||
                                  label.rfind("template-pack-param:", 0) == 0;
                       }),
        clone->labels.end());
    if (auto* named = const_cast<AstNode*>(function_name_declarator(*clone->children[1]))) {
        named->text = mangled;
    }
    clone->text = mangled;

    // A pack parameter becomes one parameter per argument, so the instance takes
    // exactly the arguments the call passes. Each copy keeps the pack's name,
    // which is what sizeof...(name) folded against above.
    if (!pack_arities.empty()) {
        auto* function =
            const_cast<AstNode*>(find_function_declarator(*clone->children[1]));
        if (function != nullptr) {
            for (std::size_t index = 0; index < function->children.size();
                 ++index) {
                auto* parameter = function->children[index].get();
                if (parameter == nullptr ||
                    parameter->kind != AstKind::ParameterDeclaration ||
                    std::find(parameter->labels.begin(), parameter->labels.end(),
                              "pack") == parameter->labels.end()) {
                    continue;
                }
                parameter->labels.erase(
                    std::remove(parameter->labels.begin(), parameter->labels.end(),
                                  "pack"),
                    parameter->labels.end());
                const auto arity = pack_arities.begin()->second;
                // A pack that received no arguments contributes no parameter at
                // all, so it is removed rather than kept.
                if (arity == 0) {
                    function->children.erase(function->children.begin() +
                                             static_cast<std::ptrdiff_t>(index));
                    break;
                }
                const auto pack_name = parameter->children.size() > 1 &&
                                               parameter->children[1]
                                           ? declarator_name(*parameter->children[1])
                                           : std::string{};
                // The pack's template parameter, so a duplicate's type can name its own
                // element rather than the head's. Without this every expanded parameter
                // took the head's type: `make_tuple('a', 1000)` narrowed the int to a char
                // and returned a truncated value. The parameter carries no labels of its
                // own; its type is the specifier in children[0].
                std::string pack_template_parameter;
                if (!parameter->children.empty() && parameter->children[0]) {
                    for (const auto& label : parameter->children[0]->labels) {
                        if (std::find(pattern->second.parameters.begin(),
                                      pattern->second.parameters.end(),
                                      label) != pattern->second.parameters.end()) {
                            pack_template_parameter = label;
                            break;
                        }
                    }
                }
                for (std::size_t copy = 1; copy < arity; ++copy) {
                    auto duplicated = clone_ast(*parameter);
                    if (!pack_template_parameter.empty() && !duplicated->children.empty() &&
                        duplicated->children[0]) {
                        for (auto& label : duplicated->children[0]->labels) {
                            if (label == pack_template_parameter) {
                                label = pack_template_parameter + std::to_string(copy);
                            }
                        }
                    }
                    if (!pack_name.empty() && duplicated->children.size() > 1 &&
                        duplicated->children[1]) {
                        auto* declarator =
                            const_cast<AstNode*>(duplicated->children[1].get());
                        while (declarator != nullptr &&
                               declarator->kind != AstKind::Declarator) {
                            declarator = declarator->children.empty()
                                             ? nullptr
                                             : declarator->children.front().get();
                        }
                        if (declarator != nullptr) {
                            declarator->text = pack_name + std::to_string(copy);
                        }
                    }
                    // Each duplicate goes after the previous one. Inserting every copy at
                    // `index + 1` pushed the earlier ones right, so a three-element pack
                    // arrived as `values, values2, values1` and the instance read its
                    // second element from the third argument.
                    function->children.insert(function->children.begin() +
                                                  static_cast<std::ptrdiff_t>(index + copy),
                                              std::move(duplicated));
                }
                break;
            }
        }
    }

    // A fold over the pack becomes a chain over the expanded parameters, which
    // exist only after the signature expansion above. The pack is named by the
    // function parameter, so the entry keyed by a template parameter is skipped.
    if (!pack_arities.empty()) {
        auto pack_name = std::string{};
        auto pack_arity = std::size_t{0};
        for (const auto& entry : pack_arities) {
            if (std::find(pattern->second.parameters.begin(),
                          pattern->second.parameters.end(),
                          entry.first) == pattern->second.parameters.end()) {
                pack_name = entry.first;
                pack_arity = entry.second;
            }
        }
        if (!pack_name.empty()) {
            const auto rewrite = [](AstNode& node, const std::string& name,
                                    std::size_t arity,
                                    const auto& self) -> void {
                if (node.kind == AstKind::FoldExpression &&
                    !node.children.empty() && node.children.front() &&
                    node.children.front()->text == name) {
                    fold_over_pack(node, name, arity, node.text);
                    return;
                }
                // `f(pack...)` forwards the pack as that many arguments. The
                // signature expansion named the parameters `name`, `name1`, ...,
                // so the marked argument becomes those identifiers.
                if (node.kind == AstKind::CallExpression &&
                    node.children.size() > 1 && node.children[1]) {
                    auto& arguments = node.children[1]->children;
                    for (std::size_t index = 0; index < arguments.size();
                         ++index) {
                        auto& argument = arguments[index];
                        if (!argument ||
                            std::find(argument->labels.begin(),
                                      argument->labels.end(),
                                      "pack-expansion") ==
                                argument->labels.end() ||
                            argument->kind != AstKind::Identifier ||
                            argument->text != name) {
                            continue;
                        }
                        std::vector<std::unique_ptr<AstNode>> expanded;
                        for (std::size_t position = 0; position < arity;
                             ++position) {
                            auto element = std::make_unique<AstNode>();
                            element->kind = AstKind::Identifier;
                            element->location = argument->location;
                            element->text =
                                position == 0
                                    ? name
                                    : name + std::to_string(position);
                            expanded.push_back(std::move(element));
                        }
                        arguments.erase(arguments.begin() +
                                        static_cast<std::ptrdiff_t>(index));
                        arguments.insert(
                            arguments.begin() +
                                static_cast<std::ptrdiff_t>(index),
                            std::make_move_iterator(expanded.begin()),
                            std::make_move_iterator(expanded.end()));
                        break;
                    }
                }
                for (auto& child : node.children) {
                    if (child) {
                        self(*child, name, arity, self);
                    }
                }
            };
            rewrite(*clone, pack_name, pack_arity, rewrite);
        }
    }

    // Analyze the clone at file scope: a template body sees its own scope and
    // the file's, never the caller's.
    auto saved_scopes = std::move(scopes_);
    scopes_.clear();
    scopes_.push_back(std::move(saved_scopes.front()));
    template_bindings_ = bindings;
    template_value_bindings_ = value_bindings;
    template_pack_arities_ = pack_arities;
    if (!value_bindings.empty()) {
        substitute_value_parameters(*clone, value_bindings);
    }
    instantiated_names_.insert(mangled);
    analyze_function_definition(*clone);
    template_bindings_.clear();
    template_value_bindings_.clear();
    template_pack_arities_.clear();
    saved_scopes.front() = std::move(scopes_.front());
    scopes_ = std::move(saved_scopes);
    instantiations_.push_back(clone.get());
    instantiation_storage_.push_back(std::move(clone));
    return mangled;
}
void Sema::analyze_declaration(const AstNode& declaration) {
    if (declaration.children.empty()) {
        return;
    }
    const auto* specifiers = declaration.children.front().get();
    if (!specifiers) {
        return;
    }
    bool is_typedef = false;
    bool is_extern = false;
    bool is_static = false;
    auto base = base_type_from_specifiers(*specifiers, is_typedef, is_extern, is_static);

    if (declaration.children.size() > 1 && declaration.children[1] &&
        declaration.children[1]->kind == AstKind::StructuredBinding) {
        const auto& binding = *declaration.children[1];
        if (binding.children.empty()) {
            return;
        }
        const auto* initializer = binding.children.back().get();
        auto bound_type = base;
        if (initializer && initializer->kind == AstKind::Identifier) {
            if (auto* symbol = lookup_ordinary(initializer->text)) {
                bound_type = symbol->type ? symbol->type : base;
            }
        } else if (initializer) {
            bound_type = analyze_initializer(*initializer, nullptr);
        }
        const auto* record =
            bound_type && bound_type->kind == TypeKind::Record
                ? bound_type->record.get()
                : nullptr;
        for (std::size_t index = 0; index + 1 < binding.children.size(); ++index) {
            const auto* name_node = binding.children[index].get();
            if (!name_node) {
                continue;
            }
            auto field_type = int_type_;
            if (record && index < record->members.size()) {
                field_type = record->members[index].type;
            } else if (bound_type && bound_type->kind == TypeKind::Array &&
                       bound_type->element && index < bound_type->array_count) {
                field_type = bound_type->element;
            }
            declare_ordinary(Symbol{SymbolKind::Variable, name_node->text, field_type,
                                    name_node->location, true, 0});
        }
        return;
    }

    for (std::size_t index = 1; index < declaration.children.size(); ++index) {
        const auto* init = declaration.children[index].get();
        if (!init || init->kind != AstKind::InitDeclarator || init->children.empty()) {
            continue;
        }
        const auto* declarator = init->children.front().get();
        if (!declarator) {
            continue;
        }
        const bool has_initializer = init->children.size() > 1;
        auto declaration_base = base;
        if (has_initializer &&
            std::find(specifiers->labels.begin(), specifiers->labels.end(), "auto") !=
                specifiers->labels.end()) {
            declaration_base = analyze_initializer(*init->children[1], nullptr);
        }
        auto type = build_declarator_type(*declarator, declaration_base);
        // An array declared without a bound takes its length from the
        // initializer. Completing the type here makes a later sizeof see the
        // real length; before this the count stayed zero and a global
        // `static const int n = sizeof(array)` initialized to zero.
        if (type && type->kind == TypeKind::Array && type->element &&
            !type->array_has_count && has_initializer &&
            init->children.size() > 1 && init->children[1]) {
            const auto* initializer = init->children[1].get();
            std::uint64_t inferred = 0;
            if (initializer->kind == AstKind::StringLiteral) {
                inferred = string_literal_length(initializer->text) + 1;
            } else if (initializer->kind == AstKind::InitializerList) {
                inferred = initializer->children.size();
            }
            if (inferred > 0) {
                auto completed = std::make_shared<Type>(*type);
                completed->array_count = inferred;
                completed->array_has_count = true;
                type = std::move(completed);
            }
        }
        // Recorded while the bindings are active, for a later reader that would
        // otherwise see the pattern's stub (ADR 0035).
        declarator_types_.emplace(declarator, type);
        if (!is_typedef && type && type->kind == TypeKind::Record && type->record &&
            record_is_abstract(*type->record)) {
            error(declarator->location,
                  "cannot instantiate abstract class '" + type->record->tag + "'");
        }
        // A record declared without an initializer is default-constructed.
        if (!is_typedef && !has_initializer && type && type->kind == TypeKind::Record &&
            type->record) {
            for (const auto* record = type->record.get(); record != nullptr;
                 record = record->base && record->base->kind == TypeKind::Record &&
                                  record->base->record
                              ? record->base->record.get()
                              : nullptr) {
                for (const auto& constructor : record->methods) {
                    if (constructor.name == record->tag && constructor.is_deleted) {
                        error(declarator->location,
                              "call to deleted constructor '" + record->tag + "::" +
                                  record->tag + "'");
                    }
                }
            }
        }
        const auto name = declarator_name(*declarator);
        if (name.empty()) {
            error(declarator->location, "declaration has no name");
            continue;
        }

        SymbolKind kind = SymbolKind::Variable;
        if (is_typedef) {
            kind = SymbolKind::Typedef;
        } else if (type && type->kind == TypeKind::Function) {
            kind = SymbolKind::Function;
        }
        Symbol symbol{kind, name, type, declarator->location, has_initializer, 0};
        if (has_initializer &&
            std::find(specifiers->labels.begin(), specifiers->labels.end(),
                      "constexpr") != specifiers->labels.end() &&
            init->children.size() > 1 && init->children[1]) {
            long long constant = 0;
            if (evaluate_constant(*init->children[1], constant)) {
                symbol.constant_value = constant;
                symbol.is_constant_value = true;
            }
        }
        declare_ordinary(std::move(symbol));

        if (has_initializer) {
            if (init->children[1] &&
                init->children[1]->kind == AstKind::ExpressionStatement) {
                // `T x(a, b)` calls the constructor whose arity matches, so a
                // deleted one of that arity is refused here. One arity can hold
                // several constructors, so the argument's shape narrows it:
                // `unique_ptr<T> p(new T)` passes a fresh allocation and
                // `T y(static_cast<T&&>(x))` an rvalue, and neither is the
                // deleted copy constructor of the same arity. The argument is
                // classified by its node kind only, because analysing it here
                // re-enters declaration analysis.
                const auto& arguments = init->children[1]->children;
                bool selects_the_record = true;
                bool argument_rvalue = false;
                if (arguments.size() == 1 && arguments.front()) {
                    const auto* argument = arguments.front().get();
                    if (argument->kind == AstKind::UnaryExpression &&
                        (argument->text == "new" || argument->text == "&")) {
                        selects_the_record = false;
                    } else if (argument->kind == AstKind::CastExpression &&
                               argument->children.size() > 1 &&
                               argument->children[0]) {
                        const auto cast_type =
                            type_from_type_name(*argument->children[0]);
                        argument_rvalue =
                            cast_type && cast_type->is_rvalue_reference;
                    }
                }
                if (selects_the_record && type && type->kind == TypeKind::Record &&
                    type->record) {
                    const auto key = type->record->tag + "#" +
                                     std::to_string(arguments.size());
                    for (const auto& constructor : type->record->methods) {
                        if (constructor.name != key || !constructor.is_deleted) {
                            continue;
                        }
                        // A deleted move constructor is chosen for an rvalue and
                        // a deleted copy constructor for an lvalue. The recorded
                        // type carries the parameter, so the categories must
                        // agree before this is the constructor being called.
                        const auto& parameters = constructor.type
                                                     ? constructor.type->parameters
                                                     : std::vector<TypePtr>{};
                        const TypePtr* reference_parameter = nullptr;
                        for (const auto& parameter : parameters) {
                            if (parameter && parameter->is_reference) {
                                reference_parameter = &parameter;
                                break;
                            }
                        }
                        // A deleted constructor whose type was not recorded
                        // is treated as a copy, which is what = delete on a
                        // reference parameter nearly always is.
                        const bool parameter_rvalue =
                            reference_parameter != nullptr &&
                            (*reference_parameter)->is_rvalue_reference;
                        if (parameter_rvalue != argument_rvalue) {
                            continue;
                        }
                        error(declarator->location,
                              "call to deleted constructor '" +
                                  type->record->tag + "::" + type->record->tag + "'");
                    }
                }
                for (const auto& argument : init->children[1]->children) {
                    if (argument) {
                        static_cast<void>(analyze_expression(*argument));
                    }
                }
            } else {
                // The initializer's own type decides whether this is a copy of a
                // record or a conversion into one; the initializer analysis below
                // reports the target type, so ask the expression directly.
                // A literal or a named value converts; so does a single-element braced
                // list in copy-list initialisation. Designated initialisers are a
                // different node and are left alone.
                const bool scalar_initializer =
                    init->children[1]->kind == AstKind::IntegerLiteral ||
                    init->children[1]->kind == AstKind::Identifier;
                const bool list_initializer =
                    init->children[1]->kind == AstKind::InitializerList &&
                    init->children[1]->children.size() == 1;
                const bool converts = type && type->kind == TypeKind::Record &&
                                      type->record &&
                                      (scalar_initializer || list_initializer);
                if (converts) {
                    const auto key = type->record->tag + "#1";
                    for (const auto& constructor : type->record->methods) {
                        if (constructor.name == key && constructor.is_deleted) {
                            error(declarator->location,
                                  "call to deleted constructor '" + type->record->tag +
                                      "::" + type->record->tag + "'");
                        }
                        if (constructor.name == key + ":explicit") {
                            error(declarator->location,
                                  "constructor '" + type->record->tag + "::" +
                                      type->record->tag +
                                      "' is explicit; use direct initialisation");
                        }
                    }
                }
                analyze_initializer(*init->children[1], type);
            }
        }
    }
}

void Sema::analyze_function_definition(const AstNode& definition) {

    // Record the template pattern the parser tagged, then analyze the
    // int-substituted definition exactly as before. (ADR 0031)
    std::vector<std::string> template_parameters;
    std::unordered_set<std::string> template_value_parameters;
    for (const auto& label : definition.labels) {
        constexpr std::string_view prefix = "template-param:";
        constexpr std::string_view value_prefix = "template-value-param:";
        if (label.rfind(value_prefix, 0) == 0) {
            const auto name = label.substr(value_prefix.size());
            template_parameters.push_back(name);
            template_value_parameters.insert(name);
        } else if (label.rfind(prefix, 0) == 0) {
            template_parameters.push_back(label.substr(prefix.size()));
        }
    }
    if (!template_parameters.empty() && !definition.text.empty() &&
        templates_.find(definition.text) == templates_.end()) {
        TemplateInfo info;
        info.parameters = template_parameters;
        info.value_parameters = template_value_parameters;
        info.definition = &definition;
        info.name = definition.text;
        templates_.emplace(definition.text, std::move(info));
    }
    if (definition.children.size() < 3) {
        return;
    }
    const auto* specifiers = definition.children[0].get();
    const auto* declarator = definition.children[1].get();
    if (!specifiers || !declarator || !definition.children[2]) {
        return;
    }
    bool is_typedef = false;
    bool is_extern = false;
    bool is_static = false;
    auto base = base_type_from_specifiers(*specifiers, is_typedef, is_extern, is_static);
    auto type = build_declarator_type(*declarator, base);
    // Recorded while an instance's bindings are active: code generation reads it
    // back after they are gone, and a reference return would otherwise look like
    // a value return (ADR 0035).
    if (type != nullptr) {
        declarator_types_.emplace(declarator, type);
    }
        if (!is_typedef && type && type->kind == TypeKind::Record && type->record &&
            record_is_abstract(*type->record)) {
            error(declarator->location,
                  "cannot instantiate abstract class '" + type->record->tag + "'");
        }
    const auto name = declarator_name(*declarator);
    if (!type || type->kind != TypeKind::Function) {
        error(declarator->location,
              "function definition '" + name + "' does not have a function type");
        return;
    }
    if (name.empty()) {
        error(declarator->location, "function definition has no name");
        return;
    }
    if (name == "main" && type->return_type &&
        type->return_type->kind == TypeKind::Void) {
        error(declarator->location, "main must return int, not void");
    }
    if (std::find(specifiers->labels.begin(), specifiers->labels.end(),
                  "constexpr") != specifiers->labels.end()) {
        constexpr_functions_[name] = &definition;
    }
    Symbol symbol{SymbolKind::Function, name, type, declarator->location, true, 0};
    declare_ordinary(std::move(symbol));

    const auto saved_function_name = current_function_name_;
    current_function_name_ = name;
    const auto saved_enclosing_record = enclosing_record_;
    enclosing_record_ = nullptr;
    if (const auto separator = name.rfind("::"); separator != std::string::npos) {
        // An out-of-line definition names its class; give the body that class's
        // members and a `this` object, which the body may use unqualified.
        const auto class_name = name.substr(0, separator);
        auto* tag = lookup_tag(class_name);
        if (tag != nullptr && tag->type && tag->type->kind == TypeKind::Record) {
            enclosing_record_ = tag->type->record.get();
        }
    }
    const bool saved_auto_return = auto_return_active_;
    auto_return_active_ =
        std::find(specifiers->labels.begin(), specifiers->labels.end(), "auto") !=
        specifiers->labels.end();
    auto_return_type_ = nullptr;
    // A template pattern whose body depends on a value parameter cannot be
    // analysed: std::array<T, N> is not a record until N is bound, so the
    // pattern is a signature only and every instance is analysed with its own
    // bindings.
    // A pattern whose body depends on a value parameter or on a pack cannot be
    // analysed: neither has a value until a call supplies one. Only the instances
    // are analysed, and code generation asks the same question of the same
    // definition so it does not emit a body that was never built.
    const bool pattern_signature = is_pattern_signature(definition);
    if (!pattern_signature) {
        analyze_function_body(definition, enclosing_record_);
    }
    if (auto_return_active_ && auto_return_type_ && type->return_type) {
        auto updated = std::make_shared<Type>(*type);
        updated->return_type = auto_return_type_;
        type = std::move(updated);
        if (auto* updated_symbol = lookup_ordinary(name)) {
            updated_symbol->type = type;
        }
    }
    auto_return_active_ = saved_auto_return;
    auto_return_type_ = nullptr;
    current_function_name_ = saved_function_name;
    enclosing_record_ = saved_enclosing_record;
}

bool Sema::is_pattern_signature(const AstNode& definition) const {
    // A value parameter or a pack has no value until a call supplies one, and
    // sizeof...(pack) is the instance's arity, so a body that depends on either
    // cannot be analysed as a program. The parser records the declarations as
    // labels on the definition -- including `template-pack-param:` for a pack that
    // never appears as a parameter pack, which is the shape a signature naming a
    // class template has (`arity_of(Tup<T, Rest...>&)`).
    const bool declared_dependent =
        std::any_of(definition.labels.begin(), definition.labels.end(),
                    [](const std::string& label) {
                        return label.rfind("template-value-param:", 0) == 0 ||
                               label.rfind("template-pack-param:", 0) == 0;
                    });
    if (declared_dependent) {
        return true;
    }
    // A parameter pack in the parameter list is the other spelling.
    for (const auto& child : definition.children) {
        if (!child) {
            continue;
        }
        for (const auto& nested : child->children) {
            if (nested && std::find(nested->labels.begin(), nested->labels.end(),
                                    "pack") != nested->labels.end()) {
                return true;
            }
        }
    }
    return false;
}

void Sema::analyze_function_body(const AstNode& definition, RecordInfo* record) {
    const auto* declarator =
        definition.children.size() > 1 ? definition.children[1].get() : nullptr;
    if (declarator == nullptr) {
        return;
    }
    push_scope();
    if (const auto* function = find_function_declarator(*declarator)) {
        analyze_parameters(*function);
    }
    if (record != nullptr) {
        // A member body sees `this` and its class's members unqualified.
        auto* tag = lookup_tag(record->tag);
        if (tag != nullptr && tag->type) {
            Symbol self;
            self.kind = SymbolKind::Variable;
            self.name = "this";
            self.type = pointer_to(tag->type);
            self.location = declarator->location;
            declare_ordinary(std::move(self));
        }
        // A constructor's member initializer list is part of its body scope.
        if (definition.children.size() > 3 && definition.children[3]) {
            for (const auto& initializer : definition.children[3]->children) {
                if (!initializer) {
                    continue;
                }
                for (const auto& arguments : initializer->children) {
                    if (!arguments) {
                        continue;
                    }
                    for (const auto& argument : arguments->children) {
                        if (argument) {
                            static_cast<void>(analyze_expression(*argument));
                        }
                    }
                }
            }
        }
    }
    if (definition.children.size() > 2 && definition.children[2]) {
        analyze_compound(*definition.children[2]);
    }
    pop_scope();
}

void Sema::analyze_parameters(const AstNode& function_declarator) {
    for (std::size_t index = 1; index < function_declarator.children.size(); ++index) {
        const auto* parameter = function_declarator.children[index].get();
        if (!parameter || parameter->kind != AstKind::ParameterDeclaration) {
            continue;
        }
        const auto name = parameter->children.size() > 1
                              ? declarator_name(*parameter->children[1])
                              : std::string{};
        auto type = parameter_type(*parameter);
        if (name.empty() || (type && type->kind == TypeKind::Void)) {
            continue;
        }
        Symbol symbol{SymbolKind::Variable, name, type, parameter->location, true, 0};
        declare_ordinary(std::move(symbol));
    }
}

void Sema::analyze_compound(const AstNode& compound) {
    push_scope();
    for (const auto& child : compound.children) {
        if (!child) {
            continue;
        }
        if (child->kind == AstKind::Declaration) {
            analyze_declaration(*child);
        } else if (child->kind == AstKind::StaticAssertDeclaration) {
            analyze_static_assert(*child);
        } else {
            analyze_statement(*child);
        }
    }
    pop_scope();
}

void Sema::analyze_statement(const AstNode& statement) {
    switch (statement.kind) {
    case AstKind::CompoundStatement:
        analyze_compound(statement);
        break;
    case AstKind::Declaration:
        analyze_declaration(statement);
        break;
    case AstKind::ExpressionStatement:
        if (!statement.children.empty()) {
            analyze_expression(*statement.children.front());
        }
        break;
    case AstKind::IfStatement:
        if (!statement.children.empty()) {
            analyze_expression(*statement.children[0]);
        }
        if (std::find(statement.labels.begin(), statement.labels.end(), "constexpr") !=
            statement.labels.end()) {
            long long condition = 1;
            if (!evaluate_constant(*statement.children[0], condition)) {
                condition = 1;
            }
            const auto branch = condition != 0 ? 1u : 2u;
            if (branch < statement.children.size() && statement.children[branch]) {
                analyze_statement(*statement.children[branch]);
            }
            break;
        }
        for (std::size_t index = 1; index < statement.children.size(); ++index) {
            if (statement.children[index]) {
                analyze_statement(*statement.children[index]);
            }
        }
        break;
    case AstKind::WhileStatement:
    case AstKind::SwitchStatement:
        if (!statement.children.empty()) {
            analyze_expression(*statement.children[0]);
        }
        if (statement.children.size() > 1 && statement.children[1]) {
            analyze_statement(*statement.children[1]);
        }
        break;
    case AstKind::DoStatement:
        if (!statement.children.empty() && statement.children[0]) {
            analyze_statement(*statement.children[0]);
        }
        if (statement.children.size() > 1 && statement.children[1]) {
            analyze_expression(*statement.children[1]);
        }
        break;
    case AstKind::ThrowStatement: {
        if (statement.children.empty() || !statement.children.front()) {
            if (handler_depth_ == 0) {
                error(statement.location,
                      "'throw;' outside an exception handler is not allowed");
            }
            break;
        }
        const auto thrown = analyze_expression(*statement.children.front());
        if (!thrown || thrown->kind != TypeKind::Record || !thrown->record) {
            error(statement.location, "throw requires a class type");
        }
        break;
    }
    case AstKind::TryStatement: {
        if (statement.children.empty() || !statement.children.front()) {
            break;
        }
        analyze_statement(*statement.children.front());
        for (std::size_t index = 1; index < statement.children.size(); ++index) {
            const auto* clause = statement.children[index].get();
            if (!clause || clause->kind != AstKind::CatchClause) {
                continue;
            }
            push_scope();
            std::size_t body_index = 0;
            if (!clause->children.empty() && clause->children.front() &&
                clause->children.front()->kind == AstKind::ParameterDeclaration) {
                const auto* parameter = clause->children.front().get();
                const auto type = parameter_type(*parameter);
                const auto name =
                    parameter->children.size() > 1 && parameter->children[1]
                        ? declarator_name(*parameter->children[1])
                        : std::string{};
                if (!name.empty() && type) {
                    declare_ordinary(Symbol{SymbolKind::Variable, name, type,
                                            parameter->location, true, 0});
                }
                body_index = 1;
            }
            ++handler_depth_;
            if (body_index < clause->children.size() && clause->children[body_index]) {
                analyze_statement(*clause->children[body_index]);
            }
            --handler_depth_;
            pop_scope();
        }
        break;
    }
    case AstKind::RangeForStatement: {
        push_scope();
        TypePtr range_type;
        bool initializer_range = false;
        if (statement.children.size() > 2 && statement.children[2]) {
            if (statement.children[2]->kind == AstKind::InitializerList) {
                initializer_range = true;
                for (const auto& element : statement.children[2]->children) {
                    if (element) {
                        analyze_expression(*element);
                    }
                }
            } else {
                range_type = decay(analyze_expression(*statement.children[2]));
            }
        }
        TypePtr element = int_type_;
        if (initializer_range) {
            element = int_type_;
        } else if (range_type && range_type->kind == TypeKind::Pointer &&
                   range_type->element) {
            element = range_type->element;
        } else if (range_type && range_type->kind == TypeKind::Array && range_type->element) {
            element = range_type->element;
        }
        if (statement.children.size() > 1 && statement.children[1]) {
            auto type = build_declarator_type(*statement.children[1], element);
            const auto name = declarator_name(*statement.children[1]);
            if (!name.empty()) {
                declare_ordinary(Symbol{SymbolKind::Variable, name, type,
                                        statement.children[1]->location, true, 0});
            }
        }
        if (statement.children.size() > 3 && statement.children[3]) {
            analyze_statement(*statement.children[3]);
        }
        pop_scope();
        break;
    }
    case AstKind::ForStatement: {
        push_scope();
        if (!statement.children.empty() && statement.children[0]) {
            if (statement.children[0]->kind == AstKind::Declaration) {
                analyze_declaration(*statement.children[0]);
            } else {
                analyze_expression(*statement.children[0]);
            }
        }
        if (statement.children.size() > 1 && statement.children[1]) {
            analyze_expression(*statement.children[1]);
        }
        if (statement.children.size() > 2 && statement.children[2]) {
            analyze_expression(*statement.children[2]);
        }
        if (statement.children.size() > 3 && statement.children[3]) {
            analyze_statement(*statement.children[3]);
        }
        pop_scope();
        break;
    }
    case AstKind::CaseStatement:
        if (!statement.children.empty()) {
            analyze_expression(*statement.children[0]);
        }
        if (statement.children.size() > 1 && statement.children[1]) {
            analyze_statement(*statement.children[1]);
        }
        break;
    case AstKind::DefaultStatement:
    case AstKind::LabelStatement:
        if (!statement.children.empty() && statement.children[0]) {
            analyze_statement(*statement.children[0]);
        }
        break;
    case AstKind::ReturnStatement:
        if (!statement.children.empty() && statement.children.front()) {
            const auto returned = analyze_expression(*statement.children.front());
            if (auto_return_active_ && !auto_return_type_) {
                auto_return_type_ = returned;
            }
        }
        break;
    case AstKind::StaticAssertDeclaration:
        analyze_static_assert(statement);
        break;
    case AstKind::BreakStatement:
    case AstKind::ContinueStatement:
    case AstKind::GotoStatement:
    case AstKind::NullStatement:
        break;
    default:
        break;
    }
}

void Sema::analyze_static_assert(const AstNode& assertion) {
    if (assertion.children.empty() || !assertion.children.front()) {
        return;
    }
    analyze_expression(*assertion.children.front());
    long long value = 0;
    if (!evaluate_constant(*assertion.children.front(), value)) {
        error(assertion.location, "static assertion is not an integer constant expression");
    } else if (value == 0) {
        error(assertion.location, "static assertion failed");
    }
}

TypePtr Sema::analyze_type_name(const AstNode& type_name) {
    if (type_name.text == "decltype") {
        if (!type_name.children.empty() && type_name.children.front()) {
            const auto* operand = type_name.children.front().get();
            std::function<TypePtr(const AstNode&)> infer_decltype =
                [&](const AstNode& node) -> TypePtr {
                switch (node.kind) {
                case AstKind::Identifier:
                    if (auto* symbol = lookup_ordinary(node.text)) {
                        return symbol->type ? symbol->type : int_type_;
                    }
                    return int_type_;
                case AstKind::IntegerLiteral:
                    return int_type_;
                case AstKind::FloatLiteral:
                    return double_type_;
                case AstKind::CastExpression:
                    return node.children.empty() || !node.children.front()
                               ? int_type_
                               : analyze_type_name(*node.children.front());
                case AstKind::BinaryExpression: {
                    if (node.children.size() < 2) {
                        return int_type_;
                    }
                    const auto left = infer_decltype(*node.children[0]);
                    const auto right = infer_decltype(*node.children[1]);
                    if ((left && left->kind == TypeKind::Double) ||
                        (right && right->kind == TypeKind::Double)) {
                        return double_type_;
                    }
                    if ((left && left->kind == TypeKind::Float) ||
                        (right && right->kind == TypeKind::Float)) {
                        return float_type_;
                    }
                    return left ? left : int_type_;
                }
                default:
                    return int_type_;
                }
            };
            return infer_decltype(*operand);
        }
        return int_type_;
    }
    if (type_name.children.empty()) {
        if (!type_name.text.empty()) {
            if (auto type = type_named(type_name.text)) {
                return type;
            }
        }
        return int_type_;
    }
    const auto* specifiers = type_name.children.front().get();
    if (!specifiers) {
        return int_type_;
    }
    bool is_typedef = false;
    bool is_extern = false;
    bool is_static = false;
    auto base = base_type_from_specifiers(*specifiers, is_typedef, is_extern, is_static);
    if (type_name.children.size() > 1) {
        if (const auto* declarator = type_name.children[1].get()) {
            base = build_declarator_type(*declarator, base);
        }
    }
    return base;
}

TypePtr Sema::analyze_initializer(const AstNode& initializer, TypePtr target) {
    if (initializer.kind == AstKind::InitializerList) {
        TypePtr element = target;
        if (target) {
            if (target->kind == TypeKind::Array) {
                element = target->element;
            } else if (target->kind == TypeKind::Record && target->record) {
                element = nullptr;
            }
        }
        for (const auto& child : initializer.children) {
            if (!child) {
                continue;
            }
            if (child->kind == AstKind::DesignatedInitializer) {
                if (!child->children.empty()) {
                    analyze_initializer(*child->children.back(), nullptr);
                }
            } else {
                analyze_initializer(*child, element);
            }
        }
        return target ? target : int_type_;
    }
    auto type = analyze_expression(initializer);
    return target ? target : type;
}

void Sema::check_dynamic_cast(const AstNode& expression, TypePtr target, TypePtr source) {
    // A reference target is checked the same way as a pointer target; the
    // failed case throws std::bad_cast rather than yielding null. Both kinds
    // carry the class as the pointer's element.
    if (!target) {
        error(expression.location, "dynamic_cast requires a target type");
        return;
    }
    const bool reference_target = target->is_reference;
    const TypePtr target_element =
        target->kind == TypeKind::Pointer ? target->element : nullptr;
    // oid* is a valid target for a pointer cast: it names the complete
    // object. With single inheritance that is the address the operand already
    // holds, so the cast is the identity. A reference to void is not valid.
    const bool void_target =
        !reference_target && target_element &&
        target_element->kind == TypeKind::Void;
    if (!target_element ||
        (target_element->kind != TypeKind::Record && !void_target)) {
        error(expression.location,
              reference_target
                  ? "dynamic_cast target must be a reference to a class"
                  : "dynamic_cast target must be a pointer to a class");
        return;
    }
    // The operand is a pointer, a reference, or an lvalue of a class type; the
    // lvalue form is what a reference target usually takes.
    const TypePtr source_element =
        source && source->kind == TypeKind::Pointer
            ? source->element
            : (source && source->kind == TypeKind::Record ? source : nullptr);
    if (!source || !source_element || source_element->kind != TypeKind::Record) {
        error(expression.location,
              "dynamic_cast operand must be a pointer or an lvalue of a class");
        return;
    }
    const auto* source_record = source_element->record.get();
    const auto* target_record = target->element->record.get();
    if (!source_record || !target_record) {
        return;
    }
    if (source_record->polymorphic) {
        return;
    }
    // A non-polymorphic source is only valid when the target is one of its
    // bases, which the compiler can check without a runtime table.
    for (const auto* current = source_record; current != nullptr;
         current = current->base && current->base->record
                       ? current->base->record.get()
                       : nullptr) {
        if (current == target_record) {
            return;
        }
    }
    error(expression.location, "dynamic_cast requires a polymorphic source class");
}

TypePtr Sema::analyze_expression(const AstNode& expression) {
    switch (expression.kind) {
    case AstKind::IntegerLiteral: {
        bool is_unsigned = false;
        int long_count = 0;
        for (auto index = expression.text.size(); index > 0; --index) {
            const char c = expression.text[index - 1];
            if (c == 'u' || c == 'U') {
                is_unsigned = true;
            } else if (c == 'l' || c == 'L') {
                ++long_count;
            } else {
                break;
            }
        }
        // C11 6.4.4.1: an unsuffixed literal takes the first type that holds it,
        // and a hexadecimal or octal literal may take the unsigned type of the
        // same width. A suffixed literal must fit the type its suffix names, so
        // an out-of-range value is diagnosed there instead of being narrowed by
        // a backend, which is how 5000000001L became 0x2001 on a 16-bit profile.
        const auto width_of = [&](TypeKind candidate) -> std::uint32_t {
            switch (candidate) {
            case TypeKind::Int:
                return profile_.int_bits;
            case TypeKind::Long:
                return profile_.long_bits;
            case TypeKind::LongLong:
                return profile_.long_long_bits;
            default:
                return 0;
            }
        };
        const auto parsed = parse_integer_literal(expression.text);
        if (!parsed) {
            return int_type_;
        }
        const auto fits = [&](TypeKind candidate, bool candidate_unsigned) -> bool {
            const auto bits = width_of(candidate);
            if (bits == 0 || bits >= 64) {
                return bits >= 64;
            }
            const auto maximum =
                candidate_unsigned
                    ? (static_cast<std::int64_t>(1) << bits) - 1
                    : (static_cast<std::int64_t>(1) << (bits - 1)) - 1;
            return *parsed <= maximum;
        };
        const bool prefixed_base =
            expression.text.size() > 1 && expression.text.front() == '0';
        if (long_count > 0 || is_unsigned) {
            const auto chosen = long_count >= 2   ? TypeKind::LongLong
                                : long_count == 1 ? TypeKind::Long
                                                  : TypeKind::Int;
            if (!fits(chosen, is_unsigned)) {
                error(expression.location,
                      "integer constant is too large for its type");
            }
            return make_builtin(chosen, is_unsigned);
        }
        for (const auto candidate : {TypeKind::Int, TypeKind::Long,
                                     TypeKind::LongLong}) {
            if (fits(candidate, false)) {
                return make_builtin(candidate, false);
            }
            if (prefixed_base && fits(candidate, true)) {
                return make_builtin(candidate, true);
            }
        }
        error(expression.location, "integer constant is too large for its type");
        return make_builtin(TypeKind::LongLong, true);
    }
    case AstKind::FloatLiteral:
        return !expression.text.empty() &&
                       (expression.text.back() == 'f' || expression.text.back() == 'F')
                   ? float_type_
                   : double_type_;
    case AstKind::CharLiteral:
        // C++ gives a character literal the type char; C gives it int. Typing it int in
        // C++ made `make_tuple(41, 'a')` deduce `tuple<int, int>`, which could not be
        // assigned to the `tuple<int, char>` it was written into.
        return cxx_mode_ ? char_type_ : int_type_;
    case AstKind::StringLiteral:
        return pointer_to(char_type_);
    case AstKind::LambdaExpression: {
        if (expression.children.empty() || !expression.children.front()) {
            return pointer_to(function_of(int_type_, {}, false, true));
        }
        const auto& declarator = *expression.children.front();
        std::vector<TypePtr> parameters;
        for (std::size_t index = 1; index < declarator.children.size(); ++index) {
            const auto* parameter = declarator.children[index].get();
            if (parameter && parameter->kind == AstKind::ParameterDeclaration) {
                parameters.push_back(parameter_type(*parameter));
            }
        }
        const auto lambda_type = function_of(int_type_, parameters, false, true);
        const bool saved_auto_return = auto_return_active_;
        auto_return_active_ = true;
        auto_return_type_ = nullptr;
        push_scope();
        std::size_t parameter_index = 0;
        for (std::size_t index = 1; index < declarator.children.size(); ++index) {
            const auto* parameter = declarator.children[index].get();
            if (!parameter || parameter->kind != AstKind::ParameterDeclaration) {
                continue;
            }
            if (parameter->children.size() > 1 && parameter->children[1]) {
                const auto name = declarator_name(*parameter->children[1]);
                if (!name.empty() && parameter_index < parameters.size()) {
                    declare_ordinary(Symbol{SymbolKind::Variable, name,
                                            parameters[parameter_index],
                                            parameter->location, true, 0});
                }
            }
            ++parameter_index;
        }
        for (std::size_t index = 2; index < expression.children.size();
             ++index) {
            const auto* capture = expression.children[index].get();
            if (!capture || capture->kind != AstKind::LambdaCapture ||
                capture->children.empty() || !capture->children.front()) {
                continue;
            }
            const auto capture_type =
                analyze_expression(*capture->children.front());
            expression_types_[capture->children.front().get()] = capture_type;
            declare_ordinary(Symbol{SymbolKind::Variable, capture->text,
                                    capture_type, capture->location, true, 0});
        }
        if (expression.children.size() > 1 && expression.children[1]) {
            analyze_statement(*expression.children[1]);
        }
        pop_scope();
        TypePtr return_type = int_type_;
        if (auto_return_type_) {
            return_type = auto_return_type_;
        }
        auto_return_active_ = saved_auto_return;
        auto_return_type_ = nullptr;
        auto function_type = std::make_shared<Type>(*lambda_type);
        function_type->return_type = return_type;
        auto result = pointer_to(std::move(function_type));
        expression_types_[&expression] = result;
        return result;
    }
    case AstKind::Identifier:
        // A value parameter used as a value folds here, so code generation can
        // still see the number after the bindings are gone (ADR 0033).
        if (const auto bound = template_value_bindings_.find(expression.text);
            bound != template_value_bindings_.end()) {
            constant_folds_[&expression] = bound->second;
            return int_type_;
        }
        if (expression.text == "__func__") {
            if (current_function_name_.empty()) {
                error(expression.location, "'__func__' is only valid inside a function");
                return pointer_to(char_type_);
            }
            return pointer_to(qualify(char_type_, true, false));
        }
        if (auto* symbol = lookup_ordinary(expression.text)) {
            return symbol->type ? symbol->type : int_type_;
        }
        // Inside a member body an unqualified name may be a member of the
        // defining class or one of its bases, or one of its static members.
        if (enclosing_record_ != nullptr) {
            if (auto* statics = lookup_ordinary(enclosing_record_->tag + "::" +
                                                expression.text)) {
                return statics->type ? statics->type : int_type_;
            }
        }
        for (const auto* record = enclosing_record_; record != nullptr;
             record = record->base && record->base->record
                          ? record->base->record.get()
                          : nullptr) {
            for (const auto& member : record->members) {
                if (member.name == expression.text) {
                    check_member_access(expression.location, *record, member.name,
                                        member.access);
                    return member.type;
                }
            }
            for (const auto& method : record->methods) {
                if (method.name == expression.text) {
                    return method.type;
                }
            }
        }
        error(expression.location, "use of undeclared identifier '" + expression.text + "'");
        return int_type_;
    case AstKind::UnaryExpression: {
        if (expression.children.empty() || !expression.children.front()) {
            return int_type_;
        }
        const auto& operation = expression.text;
        if (operation == "placement_new") {
            if (expression.children.size() > 1 && expression.children[1] &&
                expression.children[1]->kind == AstKind::TypeName) {
                const auto type = analyze_type_name(*expression.children[1]);
                if (type) {
                    return pointer_to(type);
                }
            }
            return pointer_to(int_type_);
        }
        if (operation == "new") {
            if (expression.children.front()->kind == AstKind::TypeName) {
                const auto type = analyze_type_name(*expression.children.front());
                if (type) {
                    return pointer_to(type);
                }
            }
            return pointer_to(int_type_);
        }
        auto operand = analyze_expression(*expression.children.front());
        if (operation == "delete" || operation == "delete[]") {
            return void_type_;
        }
        if (operation == "*") {
            const auto decayed = decay(operand);
            if (decayed && decayed->kind == TypeKind::Pointer && decayed->element) {
                return decayed->element;
            }
            // A class may overload dereference, which *owner uses.
            if (operand && operand->kind == TypeKind::Record && operand->record) {
                for (auto* record = operand->record.get(); record != nullptr;
                     record = record->base && record->base->record
                                  ? record->base->record.get()
                                  : nullptr) {
                    for (const auto& method : record->methods) {
                        if (method.name == "operator*" && method.type) {
                            return method.type->return_type;
                        }
                    }
                }
            }
            if (operand && operand->kind == TypeKind::Function) {
                return operand;
            }
            error(expression.location, "cannot dereference a non-pointer value");
            return int_type_;
        }
        if (operation == "&") {
            // The address of a reference variable is the address of its
            // referent, so the pointer's element is the referent's type; taking
            // the reference itself yields ptr<T&> and nothing can use it.
            if (operand && operand->is_reference && operand->element) {
                return pointer_to(operand->element);
            }
            return pointer_to(operand);
        }
        if (operation == "!") {
            return int_type_;
        }
        if (operation == "~") {
            return promote(operand);
        }
        if (operation == "+" || operation == "-") {
            return promote(operand);
        }
        return operand;
    }
    case AstKind::PostfixExpression:
        if (!expression.children.empty() && expression.children.front()) {
            return analyze_expression(*expression.children.front());
        }
        return int_type_;
    case AstKind::BinaryExpression: {
        if (expression.children.size() < 2 || !expression.children[0] ||
            !expression.children[1]) {
            return int_type_;
        }
        auto left = analyze_expression(*expression.children[0]);
        auto right = analyze_expression(*expression.children[1]);
        const auto& operation = expression.text;
        if (left && left->kind == TypeKind::Record && left->record) {
            const auto name = "operator" + operation;
            for (auto* record = left->record.get(); record != nullptr;
                 record = record->base && record->base->record
                              ? record->base->record.get()
                              : nullptr) {
                for (const auto& method : record->methods) {
                    if (method.name == name && method.type) {
                        return method.type->return_type;
                    }
                }
            }
            if (auto* function = lookup_ordinary(name);
                function && function->type &&
                function->type->kind == TypeKind::Function) {
                return function->type->return_type;
            }
        }
        const bool comparison = operation == "==" || operation == "!=" ||
                                operation == "<" || operation == ">" ||
                                operation == "<=" || operation == ">=";
        // Only when neither side is something a record could convert to: a
        // record beside a scalar compares through its conversion operator.
        if (comparison &&
            (left && left->kind == TypeKind::Record) &&
            (right && right->kind == TypeKind::Record)) {
            // The operator lookup above found nothing on the record, and a record
            // has no scalar form to compare. Reporting it here beats emitting a
            // comparison of aggregate values, which the IR verifier then rejects
            // as a compiler bug.
            const auto& record =
                left && left->kind == TypeKind::Record && left->record
                    ? left->record->tag
                    : (right && right->record ? right->record->tag
                                              : std::string{});
            error(expression.location,
                  "no matching 'operator" + operation +
                      "' for operands of record type '" + record + "'");
            return int_type_;
        }
        if (operation == ",") {
            return right;
        }
        if (operation == "&&" || operation == "||" || operation == "==" ||
            operation == "!=" || operation == "<" || operation == ">" ||
            operation == "<=" || operation == ">=") {
            return int_type_;
        }
        if (operation == "+") {
            // `array + n` has pointer type, not element type: decay first, because
            // is_pointer answers false for an Array and the arithmetic fallback then
            // decayed all the way to the element, so `*(values + 3)` was rejected as
            // a dereference of a non-pointer.
            if (const auto pointer = decay(left);
                pointer && pointer->kind == TypeKind::Pointer) {
                return pointer;
            }
            if (const auto pointer = decay(right);
                pointer && pointer->kind == TypeKind::Pointer) {
                return pointer;
            }
            return usual_arithmetic(left, right);
        }
        if (operation == "-") {
            if (left && is_pointer(*left) && right && is_pointer(*right)) {
                return ptrdiff_type_;
            }
            if (const auto pointer = decay(left);
                pointer && pointer->kind == TypeKind::Pointer) {
                return pointer;
            }
            return usual_arithmetic(left, right);
        }
        if (operation == "<<" || operation == ">>" || operation == "&" ||
            operation == "|" || operation == "^" || operation == "%") {
            return promote(left);
        }
        return usual_arithmetic(left, right);
    }
    case AstKind::AssignmentExpression: {
        if (expression.children.size() < 2 || !expression.children[0] ||
            !expression.children[1]) {
            return int_type_;
        }
        auto left = analyze_expression(*expression.children[0]);
        analyze_expression(*expression.children[1]);
        if (left && (left->kind == TypeKind::Array || left->kind == TypeKind::Function ||
                     left->is_const)) {
            error(expression.location, "assignment to a non-modifiable lvalue");
        }
        // A member of a const object is not assignable even though the member's
        // own type carries no qualifier — unless it is declared `mutable`.
        if (expression.children[0]->kind == AstKind::MemberExpression &&
            !expression.children[0]->children.empty() &&
            expression.children[0]->children.front()) {
            auto base = analyze_expression(*expression.children[0]->children.front());
            if (base && base->kind == TypeKind::Pointer) {
                base = base->element;
            }
            if (base && base->kind == TypeKind::Record && base->is_const) {
                bool mutable_member = false;
                for (const auto* record = base->record.get();
                     record != nullptr && !mutable_member;
                     record = record->base && record->base->record
                                  ? record->base->record.get()
                                  : nullptr) {
                    for (const auto& member : record->members) {
                        if (member.name == expression.children[0]->text &&
                            member.is_mutable) {
                            mutable_member = true;
                            break;
                        }
                    }
                }
                if (!mutable_member) {
                    error(expression.location, "assignment to a member of a const object");
                }
            }
        }
        return left;
    }
    case AstKind::ConditionalExpression: {
        if (expression.children.size() < 3 || !expression.children[0] ||
            !expression.children[1] || !expression.children[2]) {
            return int_type_;
        }
        analyze_expression(*expression.children[0]);
        auto when_true = analyze_expression(*expression.children[1]);
        auto when_false = analyze_expression(*expression.children[2]);
        // Code generation needs the operator's own type: the branches may be
        // names it cannot infer, and re-analysis is impossible once the
        // function's scopes are gone.
        auto conditional_type = (when_true && when_false && is_arithmetic(*when_true) &&
                                 is_arithmetic(*when_false))
                                    ? usual_arithmetic(when_true, when_false)
                                    : (when_true ? when_true : when_false);
        expression_types_[&expression] = conditional_type;
        return conditional_type;
    }
    case AstKind::CommaExpression:
        if (expression.children.size() >= 2 && expression.children[0] &&
            expression.children[1]) {
            analyze_expression(*expression.children[0]);
            return analyze_expression(*expression.children[1]);
        }
        return int_type_;
    case AstKind::CallExpression: {
        if (expression.children.size() < 2 || !expression.children[0] ||
            !expression.children[1]) {
            return int_type_;
        }
        TypePtr callee_type;
        const auto* callee = expression.children[0].get();
        if (callee->kind == AstKind::Identifier &&
            (callee->text == "std::min" ||
             callee->text == "std::max")) {
            const auto& arguments = expression.children[1]->children;
            if (arguments.empty() || !arguments.front()) {
                return int_type_;
            }
            auto first = analyze_expression(*arguments.front());
            for (std::size_t index = 1; index < arguments.size(); ++index) {
                if (arguments[index]) {
                    analyze_expression(*arguments[index]);
                }
            }
            return first;
        }
        if (callee->kind == AstKind::Identifier &&
            callee->text == "std::swap") {
            for (const auto& argument : expression.children[1]->children) {
                if (argument) {
                    analyze_expression(*argument);
                }
            }
            return void_type_;
        }
        if (callee->kind == AstKind::Identifier &&
            (callee->text == "std::move" ||
             callee->text == "std::forward")) {
            const auto& arguments = expression.children[1]->children;
            if (arguments.empty() || !arguments.front()) {
                return int_type_;
            }
            auto argument = analyze_expression(*arguments.front());
            auto reference = std::make_shared<Type>(*pointer_to(std::move(argument)));
            reference->is_reference = true;
            reference->is_rvalue_reference = true;
            return reference;
        }
        if (callee->kind == AstKind::Identifier) {
            if (cxx_mode_ && callee->text.find("::") != std::string::npos) {
                const auto separator = callee->text.rfind("::");
                const auto class_name = callee->text.substr(0, separator);
                const auto method_name = callee->text.substr(separator + 2);
                // A qualified template-id is a constructor call, not a method call: the
                // template arguments are labels on the callee, and the pattern is
                // registered under the last component (`tuple`), not the qualified
                // spelling. Instantiating here makes the expression's type the instance;
                // without it `std::tuple<int, int>(3, 4)` was typed as the primary
                // pattern and the IR verifier rejected the return.
                bool is_template_id = false;
                std::vector<std::string> template_arguments;
                for (const auto& label : callee->labels) {
                    constexpr std::string_view argument_prefix = "template-arg:";
                    if (label == "template-id") {
                        is_template_id = true;
                    } else if (label.rfind(argument_prefix, 0) == 0) {
                        template_arguments.push_back(label.substr(argument_prefix.size()));
                    }
                }
                if (is_template_id && !template_arguments.empty()) {
                    for (const auto& argument : expression.children[1]->children) {
                        if (argument) {
                            analyze_expression(*argument);
                        }
                    }
                    if (const auto instance = instantiate_class_template(
                            method_name, template_arguments)) {
                        return instance;
                    }
                }
                auto* tag = lookup_tag(class_name);
                if (tag && tag->type && tag->type->kind == TypeKind::Record &&
                    tag->type->record) {
                    for (const auto& method : tag->type->record->methods) {
                        if (method.name == method_name && method.type) {
                            if (method.is_deleted) {
                                error(callee->location,
                                      "use of deleted function '" + class_name + "::" +
                                          method_name + "'");
                            }
                            check_member_access(callee->location, *tag->type->record,
                                                method.name, method.access);
                            for (const auto& argument : expression.children[1]->children) {
                                if (argument) {
                                    analyze_expression(*argument);
                                }
                            }
                            return method.type->return_type;
                        }
                    }
                }
            }
            auto* symbol = lookup_ordinary(callee->text);
            // An unqualified call inside a member body may name a member
            // function of the defining class or one of its bases.
            TypePtr member_method;
            if (!symbol && enclosing_record_ != nullptr) {
                for (const auto* record = enclosing_record_; record != nullptr;
                     record = record->base && record->base->record
                                  ? record->base->record.get()
                                  : nullptr) {
                    for (const auto& method : record->methods) {
                        if (method.name == callee->text && method.type) {
                            member_method = method.type;
                            break;
                        }
                    }
                    if (member_method) {
                        break;
                    }
                }
            }
            if (symbol && symbol->type && symbol->type->kind == TypeKind::Record &&
                symbol->type->record) {
                for (auto* record = symbol->type->record.get(); record != nullptr;
                     record = record->base && record->base->record
                                  ? record->base->record.get()
                                  : nullptr) {
                    for (const auto& method : record->methods) {
                        if (method.name == "operator()" && method.type) {
                            for (const auto& argument : expression.children[1]->children) {
                                if (argument) {
                                    analyze_expression(*argument);
                                }
                            }
                            return method.type->return_type;
                        }
                    }
                }
                if (symbol->kind == SymbolKind::RecordTag ||
                    symbol->kind == SymbolKind::Typedef) {
                    for (const auto& argument : expression.children[1]->children) {
                        if (argument) {
                            analyze_expression(*argument);
                        }
                    }
                    // `P<int>()` names a template-id: the parser records the arguments as
                    // labels on the callee. Instantiating here, during analysis, means the
                    // class instantiation exists before code generation collects class
                    // methods; returning the symbol's type gave the pattern instead, so a
                    // member call on the temporary found no member.
                    bool is_template_id = false;
                    std::vector<std::string> template_arguments;
                    for (const auto& label : callee->labels) {
                        constexpr std::string_view argument_prefix = "template-arg:";
                        if (label == "template-id") {
                            is_template_id = true;
                        } else if (label.rfind(argument_prefix, 0) == 0) {
                            template_arguments.push_back(label.substr(argument_prefix.size()));
                        }
                    }
                    if (is_template_id && !template_arguments.empty()) {
                        template_arguments = expand_template_arguments(template_arguments);
                        if (const auto instance = instantiate_class_template(
                                callee->text, template_arguments)) {
                            expression_types_[&expression] = instance;
                            return instance;
                        }
                    }
                    return symbol->type;
                }
                error(expression.location, "called object has no operator()");
                return int_type_;            }
            if (!symbol && callee->text == "__builtin_expect") {
                callee_type = function_of(long_type_, {long_type_, long_type_}, false, true);
            } else if (!symbol && callee->text == "__builtin_constant_p") {
                callee_type = function_of(int_type_, {int_type_}, false, true);
            } else if (!symbol && callee->text == "__builtin_unreachable") {
                callee_type = function_of(void_type_, {}, false, true);
            } else if (!symbol && member_method) {
                callee_type = member_method;
            } else if (!symbol) {
                if (profile_.id == target::TargetId::Kr16) {
                    warning(callee->location,
                            "implicit declaration of function '" + callee->text + "'");
                } else {
                    error(callee->location,
                          "implicit declaration of function '" + callee->text + "'");
                }
                auto function_type = function_of(int_type_, {}, false, false);
                scopes_.front().ordinary.emplace(
                    callee->text,
                    Symbol{SymbolKind::Function, callee->text, function_type,
                           callee->location, true, 0});
                callee_type = function_type;
            } else {
                callee_type = symbol->type;
            }
        } else {
            callee_type = analyze_expression(*callee);
        }

        std::vector<TypePtr> argument_types;
        for (const auto& argument : expression.children[1]->children) {
            if (argument) {
                argument_types.push_back(analyze_expression(*argument));
            }
        }
        // A template callee with a non-int deduction instantiates its pattern
        // and the call targets that definition (ADR 0031).
        if (callee && callee->kind == AstKind::Identifier) {
            std::vector<std::string> explicit_arguments;
            for (const auto& label : callee->labels) {
                constexpr std::string_view prefix = "template-arg:";
                if (label.rfind(prefix, 0) == 0) {
                    explicit_arguments.push_back(label.substr(prefix.size()));
                }
            }
            const auto instantiated =
                instantiate_template_call(*callee, explicit_arguments, argument_types);
            if (!instantiated.empty()) {
                call_targets_[&expression] = instantiated;
                if (const auto* instantiated_symbol = lookup_ordinary(instantiated)) {
                    callee_type = instantiated_symbol->type;
                }
            }
        }

        TypePtr function = callee_type;
        if (function && function->kind == TypeKind::Pointer && function->element &&
            function->element->kind == TypeKind::Function) {
            function = function->element;
        }
        if (!function || function->kind != TypeKind::Function) {
            error(expression.location, "called object is not a function");
            return int_type_;
        }
        if (function->has_prototype) {
            if (function->variadic) {
                if (argument_types.size() < function->parameters.size()) {
                    error(expression.location, "too few arguments to function call");
                }
            } else if (argument_types.size() != function->parameters.size()) {
                const auto required =
                    function->parameters.size() > function->default_argument_count
                        ? function->parameters.size() - function->default_argument_count
                        : 0;
                if (argument_types.size() < required ||
                    argument_types.size() > function->parameters.size()) {
                    error(expression.location, "wrong number of arguments to function call");
                }
            }
        }
        return function->return_type ? function->return_type : int_type_;
    }
    case AstKind::SubscriptExpression: {
        if (expression.children.size() < 2 || !expression.children[0] ||
            !expression.children[1]) {
            return int_type_;
        }
        auto base = decay(analyze_expression(*expression.children[0]));
        analyze_expression(*expression.children[1]);
        if (base && base->kind == TypeKind::Record && base->record) {
            for (auto* record = base->record.get(); record != nullptr;
                 record = record->base && record->base->record
                              ? record->base->record.get()
                              : nullptr) {
                for (const auto& method : record->methods) {
                    if (method.name == "operator[]" && method.type) {
                        return method.type->return_type;
                    }
                }
            }
        }
        if (base && base->kind == TypeKind::Pointer && base->element) {
            return base->element;
        }
        error(expression.location, "subscripted value is not an array or pointer");
        return int_type_;
    }
    case AstKind::MemberExpression: {
        if (expression.children.empty() || !expression.children.front()) {
            return int_type_;
        }
        auto base = analyze_expression(*expression.children.front());
        const bool arrow = !expression.labels.empty() && expression.labels.front() == "->";
        if (arrow) {
            base = decay(base);
            // A class may overload the arrow; the member then lives in the
            // record the operator returns.
            if (base && base->kind == TypeKind::Record && base->record) {
                for (const auto* record = base->record.get(); record != nullptr;
                     record = record->base && record->base->record
                                  ? record->base->record.get()
                                  : nullptr) {
                    bool found = false;
                    for (const auto& method : record->methods) {
                        if (method.name != "operator->" || !method.type ||
                            !method.type->return_type) {
                            continue;
                        }
                        const auto returned = method.type->return_type;
                        base = returned->kind == TypeKind::Pointer && returned->element
                                   ? returned->element
                                   : returned;
                        found = true;
                        break;
                    }
                    if (found) {
                        break;
                    }
                }
            }
            if (base && base->kind == TypeKind::Pointer) {
                base = base->element;
            }
        }
        if (base && base->kind == TypeKind::Pointer && base->is_reference) {
            base = base->element;
        }
        if (!base || base->kind != TypeKind::Record || !base->record) {
            error(expression.location, "member access on a non-record value");
            return int_type_;
        }
        if (!base->record->complete) {
            error(expression.location, "member access on an incomplete record");
            return int_type_;
        }
        for (const auto* record = base->record.get(); record != nullptr;
             record = record->base && record->base->record
                          ? record->base->record.get()
                          : nullptr) {
            for (const auto& member : record->members) {
                if (member.name == expression.text) {
                    check_member_access(expression.location, *record, member.name,
                                        member.access);
                    return member.type;
                }
            }
            // Two overloads may differ only in constness, so a const object
            // prefers the const one instead of taking whichever came first.
            for (const auto& method : record->methods) {
                if (method.name == expression.text) {
                    if (base->is_const && !method.is_const) {
                        bool has_const_overload = false;
                        for (const auto& candidate : record->methods) {
                            if (candidate.name == expression.text &&
                                candidate.is_const) {
                                has_const_overload = true;
                                break;
                            }
                        }
                        if (has_const_overload) {
                            continue;
                        }
                    }
                    if (method.is_deleted) {
                        error(expression.location, "use of deleted function '" +
                                                       record->tag + "::" + method.name + "'");
                    }
                    // A const object may only call members that promise not to
                    // modify it.
                    if (!method.is_const && base->is_const) {
                        error(expression.location, "call to non-const member function '" +
                                                       record->tag + "::" + method.name +
                                                       "' on a const object");
                    }
                    check_member_access(expression.location, *record, method.name,
                                        method.access);
                    return method.type;
                }
            }
        }
        error(expression.location, "no member named '" + expression.text + "'");
        return int_type_;
    }
    case AstKind::CastExpression:
        if (expression.children.size() < 2 || !expression.children[0] ||
            !expression.children[1]) {
            return int_type_;
        }
        {
            const auto operand_type = analyze_expression(*expression.children[1]);
            const auto target = analyze_type_name(*expression.children[0]);
            if (!expression.labels.empty() &&
                expression.labels.front() == "dynamic_cast") {
                check_dynamic_cast(expression, target, operand_type);
                // IR generation cannot re-analyse after the scopes are gone.
                cast_targets_[&expression] = target;
                cast_operands_[&expression] = operand_type;
            }
            return target;
        }
    case AstKind::TypeIdExpression: {
        if (expression.children.empty() || !expression.children.front()) {
            error(expression.location, "typeid has no operand");
            return int_type_;
        }
        const auto& operand = *expression.children.front();
        TypePtr operand_type = nullptr;
        if (operand.kind == AstKind::TypeName) {
            operand_type = analyze_type_name(operand);
        } else {
            operand_type = analyze_expression(operand);
        }
        typeid_operands_[&expression] = operand_type;
        if (const auto info = type_named("std::type_info")) {
            return info;
        }
        if (const auto info = type_named("type_info")) {
            return info;
        }
        error(expression.location, "typeid requires <typeinfo>");
        return int_type_;
    }
    case AstKind::SizeofExpression:
    case AstKind::AlignofExpression:
    case AstKind::OffsetofExpression: {
        // sizeof...(pack) is the instance's arity, not a type's size.
        if (expression.text == "sizeof..." && !expression.children.empty() &&
            expression.children.front()) {
            const auto* operand = expression.children.front().get();
            const auto arity = template_pack_arities_.find(operand->text);
            if (arity != template_pack_arities_.end()) {
                constant_folds_[&expression] =
                    static_cast<long long>(arity->second);
                return size_type_;
            }
            error(expression.location,
                  "sizeof... names something that is not a parameter pack");
            return size_type_;
        }
        if (!expression.children.empty() && expression.children.front()) {
            const auto* operand = expression.children.front().get();
            if (operand->kind == AstKind::TypeName) {
                static_cast<void>(analyze_type_name(*operand));
            } else {
                static_cast<void>(analyze_expression(*operand));
            }
        }
        return size_type_;
    }
    case AstKind::CompoundLiteral:
        if (expression.children.empty() || !expression.children.front()) {
            return int_type_;
        }
        if (expression.children.size() > 1 && expression.children[1]) {
            analyze_initializer(*expression.children[1], nullptr);
        }
        return analyze_type_name(*expression.children.front());
    case AstKind::GenericSelection: {
        TypePtr result;
        bool analyzed_control = false;
        for (const auto& child : expression.children) {
            if (!child || child->kind == AstKind::TypeName) {
                continue;
            }
            if (!analyzed_control) {
                static_cast<void>(analyze_expression(*child));
                analyzed_control = true;
                continue;
            }
            auto association = analyze_expression(*child);
            if (!result) {
                result = association;
            }
        }
        return result ? result : int_type_;
    }
    case AstKind::InitializerList:
        for (const auto& child : expression.children) {
            if (child) {
                analyze_initializer(*child, nullptr);
            }
        }
        return int_type_;
    default:
        error(expression.location, "unsupported expression in semantic analysis");
        return int_type_;
    }
}

bool Sema::evaluate_constexpr_body(const AstNode& statement, long long& value) {
    switch (statement.kind) {
    case AstKind::CompoundStatement:
        for (const auto& child : statement.children) {
            if (!child) {
                continue;
            }
            long long result = 0;
            if (evaluate_constexpr_body(*child, result)) {
                value = result;
                return true;
            }
        }
        return false;
    case AstKind::ReturnStatement:
        if (statement.children.empty() || !statement.children.front()) {
            return false;
        }
        return evaluate_constant(*statement.children.front(), value);
    case AstKind::IfStatement: {
        if (statement.children.empty() || !statement.children.front()) {
            return false;
        }
        long long condition = 0;
        if (!evaluate_constant(*statement.children.front(), condition)) {
            return false;
        }
        const auto branch = condition != 0 ? 1u : 2u;
        if (branch < statement.children.size() && statement.children[branch]) {
            return evaluate_constexpr_body(*statement.children[branch], value);
        }
        return false;
    }
    case AstKind::WhileStatement: {
        if (statement.children.size() < 2 || !statement.children[0] ||
            !statement.children[1]) {
            return false;
        }
        for (int iteration = 0; iteration < 128; ++iteration) {
            long long condition = 0;
            if (!evaluate_constant(*statement.children[0], condition) ||
                condition == 0) {
                break;
            }
            long long result = 0;
            if (evaluate_constexpr_body(*statement.children[1], result)) {
                value = result;
                return true;
            }
        }
        return false;
    }
    case AstKind::DoStatement: {
        if (statement.children.size() < 2 || !statement.children[0] ||
            !statement.children[1]) {
            return false;
        }
        for (int iteration = 0; iteration < 128; ++iteration) {
            long long result = 0;
            if (evaluate_constexpr_body(*statement.children[0], result)) {
                value = result;
                return true;
            }
            long long condition = 0;
            if (!evaluate_constant(*statement.children[1], condition) ||
                condition == 0) {
                break;
            }
        }
        return false;
    }
    case AstKind::ForStatement: {
        if (!statement.children.empty() && statement.children[0]) {
            long long ignored = 0;
            static_cast<void>(
                evaluate_constexpr_body(*statement.children[0], ignored));
        }
        for (int iteration = 0; iteration < 128; ++iteration) {
            if (statement.children.size() > 1 && statement.children[1]) {
                long long condition = 0;
                if (!evaluate_constant(*statement.children[1], condition) ||
                    condition == 0) {
                    break;
                }
            }
            if (statement.children.size() > 3 && statement.children[3]) {
                long long result = 0;
                if (evaluate_constexpr_body(*statement.children[3], result)) {
                    value = result;
                    return true;
                }
            }
            if (statement.children.size() > 2 && statement.children[2]) {
                long long ignored = 0;
                static_cast<void>(
                    evaluate_constant(*statement.children[2], ignored));
            }
        }
        return false;
    }
    case AstKind::Declaration: {
        for (std::size_t index = 1; index < statement.children.size(); ++index) {
            const auto* init = statement.children[index].get();
            if (!init || init->kind != AstKind::InitDeclarator ||
                init->children.size() < 2 || !init->children[1]) {
                continue;
            }
            long long initial = 0;
            if (evaluate_constant(*init->children[1], initial)) {
                constant_locals_[init->text] = initial;
            }
        }
        return false;
    }
    default:
        return false;
    }
}

std::optional<long long> Sema::parse_integer_literal(std::string_view text) const {
    while (!text.empty()) {
        const char last = text.back();
        if (last == 'u' || last == 'U' || last == 'l' || last == 'L') {
            text.remove_suffix(1);
        } else {
            break;
        }
    }
    if (text.empty()) {
        return std::nullopt;
    }
    int base = 10;
    std::size_t index = 0;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        index = 2;
    } else if (text.size() > 1 && text[0] == '0') {
        base = 8;
        index = 1;
    }
    unsigned long long value = 0;
    bool saw_digit = false;
    for (; index < text.size(); ++index) {
        const char c = text[index];
        unsigned digit = 0;
        if (c >= '0' && c <= '9') {
            digit = static_cast<unsigned>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<unsigned>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            digit = static_cast<unsigned>(c - 'A' + 10);
        } else {
            return std::nullopt;
        }
        if (digit >= static_cast<unsigned>(base)) {
            return std::nullopt;
        }
        value = value * static_cast<unsigned>(base) + digit;
        saw_digit = true;
    }
    if (!saw_digit) {
        return std::nullopt;
    }
    return static_cast<long long>(value);
}

std::optional<long long> Sema::parse_character_literal(std::string_view text) const {
    if (text.size() < 3 || text.front() != '\'') {
        return std::nullopt;
    }
    std::size_t index = 1;
    long long value = 0;
    if (text[index] == '\\') {
        ++index;
        if (index >= text.size()) {
            return std::nullopt;
        }
        const char escape = text[index++];
        switch (escape) {
        case 'n':
            value = '\n';
            break;
        case 't':
            value = '\t';
            break;
        case 'r':
            value = '\r';
            break;
        case 'a':
            value = 7;
            break;
        case 'b':
            value = 8;
            break;
        case 'f':
            value = 12;
            break;
        case 'v':
            value = 11;
            break;
        case '?':
            value = '?';
            break;
        case '\\':
            value = '\\';
            break;
        case '\'':
            value = '\'';
            break;
        case '"':
            value = '"';
            break;
        case '0':
            value = 0;
            break;
        case 'x': {
            value = 0;
            int digits = 0;
            while (index < text.size()) {
                const char character = text[index];
                int digit = -1;
                if (character >= '0' && character <= '9') {
                    digit = character - '0';
                } else if (character >= 'a' && character <= 'f') {
                    digit = character - 'a' + 10;
                } else if (character >= 'A' && character <= 'F') {
                    digit = character - 'A' + 10;
                }
                if (digit < 0) {
                    break;
                }
                value = value * 16 + digit;
                digits++;
                index++;
            }
            if (digits == 0) {
                return std::nullopt;
            }
            break;
        }
        default:
            if (escape >= '0' && escape <= '7') {
                value = escape - '0';
                for (int i = 0; i < 2 && index < text.size() && text[index] >= '0' &&
                                text[index] <= '7';
                     ++i) {
                    value = value * 8 + (text[index++] - '0');
                }
            } else {
                return std::nullopt;
            }
            break;
        }
    } else {
        value = static_cast<unsigned char>(text[index++]);
    }
    if (index >= text.size() || text[index] != '\'') {
        return std::nullopt;
    }
    return value;
}

bool Sema::evaluate_constant(const AstNode& expression, long long& value) {
    switch (expression.kind) {
    case AstKind::IntegerLiteral: {
        const auto parsed = parse_integer_literal(expression.text);
        if (!parsed) {
            return false;
        }
        value = *parsed;
        return true;
    }
    case AstKind::CharLiteral: {
        const auto parsed = parse_character_literal(expression.text);
        if (!parsed) {
            return false;
        }
        value = *parsed;
        return true;
    }
    case AstKind::Identifier:
        if (const auto bound = template_value_bindings_.find(expression.text);
            bound != template_value_bindings_.end()) {
            value = bound->second;
            constant_folds_[&expression] = value;
            return true;
        }
        if (const auto local = constant_locals_.find(expression.text);
            local != constant_locals_.end()) {
            value = local->second;
            return true;
        }
        if (auto* symbol = lookup_ordinary(expression.text);
            symbol && (symbol->kind == SymbolKind::EnumConstant ||
                       symbol->is_constant_value)) {
            value = symbol->constant_value;
            return true;
        }
        return false;
    case AstKind::CallExpression: {
        if (constant_depth_ > 32 || expression.children.size() < 2 ||
            !expression.children[0] ||
            expression.children[0]->kind != AstKind::Identifier) {
            return false;
        }
        const auto function =
            constexpr_functions_.find(expression.children[0]->text);
        if (function == constexpr_functions_.end() || !function->second) {
            return false;
        }
        const auto* definition = function->second;
        if (definition->children.size() < 3 || !definition->children[1] ||
            !definition->children[2]) {
            return false;
        }
        const auto* declarator = definition->children[1].get();
        std::vector<std::string> parameter_names;
        for (std::size_t index = 1; index < declarator->children.size(); ++index) {
            const auto* parameter = declarator->children[index].get();
            if (parameter && parameter->kind == AstKind::ParameterDeclaration &&
                parameter->children.size() > 1 && parameter->children[1]) {
                parameter_names.push_back(declarator_name(*parameter->children[1]));
            }
        }
        const auto& arguments = expression.children[1]->children;
        std::unordered_map<std::string, long long> values;
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            if (!arguments[index] || index >= parameter_names.size()) {
                return false;
            }
            long long argument = 0;
            if (!evaluate_constant(*arguments[index], argument)) {
                return false;
            }
            values[parameter_names[index]] = argument;
        }
        const auto saved = std::move(constant_locals_);
        constant_locals_ = std::move(values);
        ++constant_depth_;
        const bool evaluated =
            evaluate_constexpr_body(*definition->children[2], value);
        --constant_depth_;
        constant_locals_ = saved;
        return evaluated;
    }
    case AstKind::UnaryExpression: {
        if (expression.children.empty() || !expression.children.front()) {
            return false;
        }
        if ((expression.text == "++" || expression.text == "--") &&
            expression.children.front()->kind == AstKind::Identifier) {
            const auto local = constant_locals_.find(expression.children.front()->text);
            if (local == constant_locals_.end()) {
                return false;
            }
            local->second += expression.text == "++" ? 1 : -1;
            value = local->second;
            return true;
        }
        long long operand = 0;
        if (!evaluate_constant(*expression.children.front(), operand)) {
            return false;
        }
        if (expression.text == "+") {
            value = operand;
        } else if (expression.text == "-") {
            value = -operand;
        } else if (expression.text == "!") {
            value = operand == 0 ? 1 : 0;
        } else if (expression.text == "~") {
            value = ~operand;
        } else {
            return false;
        }
        return true;
    }
    case AstKind::AssignmentExpression: {
        if (expression.children.size() < 2 || !expression.children[0] ||
            !expression.children[1] ||
            expression.children[0]->kind != AstKind::Identifier ||
            expression.text != "=") {
            return false;
        }
        long long result = 0;
        if (!evaluate_constant(*expression.children[1], result)) {
            return false;
        }
        constant_locals_[expression.children[0]->text] = result;
        value = result;
        return true;
    }
    case AstKind::BinaryExpression: {
        if (expression.children.size() < 2 || !expression.children[0] ||
            !expression.children[1]) {
            return false;
        }
        long long left = 0;
        long long right = 0;
        if (!evaluate_constant(*expression.children[0], left) ||
            !evaluate_constant(*expression.children[1], right)) {
            return false;
        }
        const auto& operation = expression.text;
        if (operation == "+") {
            value = left + right;
        } else if (operation == "-") {
            value = left - right;
        } else if (operation == "*") {
            value = left * right;
        } else if (operation == "/") {
            if (right == 0) {
                return false;
            }
            value = left / right;
        } else if (operation == "%") {
            if (right == 0) {
                return false;
            }
            value = left % right;
        } else if (operation == "<<") {
            value = left << right;
        } else if (operation == ">>") {
            value = left >> right;
        } else if (operation == "&") {
            value = left & right;
        } else if (operation == "|") {
            value = left | right;
        } else if (operation == "^") {
            value = left ^ right;
        } else if (operation == "==") {
            value = left == right;
        } else if (operation == "!=") {
            value = left != right;
        } else if (operation == "<") {
            value = left < right;
        } else if (operation == ">") {
            value = left > right;
        } else if (operation == "<=") {
            value = left <= right;
        } else if (operation == ">=") {
            value = left >= right;
        } else if (operation == "&&") {
            value = left != 0 && right != 0;
        } else if (operation == "||") {
            value = left != 0 || right != 0;
        } else {
            return false;
        }
        return true;
    }
    case AstKind::ConditionalExpression: {
        if (expression.children.size() < 3 || !expression.children[0] ||
            !expression.children[1] || !expression.children[2]) {
            return false;
        }
        long long condition = 0;
        if (!evaluate_constant(*expression.children[0], condition)) {
            return false;
        }
        return evaluate_constant(*expression.children[condition != 0 ? 1 : 2], value);
    }
    case AstKind::CastExpression:
        if (expression.children.size() >= 2 && expression.children[0] &&
            expression.children[1] &&
            evaluate_constant(*expression.children[1], value)) {
            const auto cast_type = analyze_type_name(*expression.children[0]);
            if (cast_type && is_integer(*cast_type)) {
                const auto bits = static_cast<unsigned>(size_of(*cast_type) * 8);
                if (bits > 0 && bits < 64) {
                    const auto mask = (1ull << bits) - 1;
                    auto narrowed = static_cast<unsigned long long>(value) & mask;
                    if (!cast_type->is_unsigned &&
                        (narrowed & (1ull << (bits - 1))) != 0) {
                        narrowed |= ~mask;
                    }
                    value = static_cast<long long>(narrowed);
                }
            }
            return true;
        }
        return false;
    case AstKind::SizeofExpression:
    case AstKind::AlignofExpression: {
        if (expression.children.empty() || !expression.children.front()) {
            return false;
        }
        const auto* operand = expression.children.front().get();
        TypePtr type;
        if (operand->kind == AstKind::TypeName) {
            type = analyze_type_name(*operand);
        } else {
            type = analyze_expression(*operand);
        }
        if (!type) {
            return false;
        }
        value = expression.kind == AstKind::SizeofExpression
                    ? static_cast<long long>(size_of(*type))
                    : static_cast<long long>(align_of(*type));
        return true;
    }
    case AstKind::OffsetofExpression: {
        if (expression.children.empty() || !expression.children.front()) {
            return false;
        }
        const auto* type_node = expression.children.front().get();
        if (type_node->kind != AstKind::TypeName) {
            return false;
        }
        auto type = analyze_type_name(*type_node);
        std::vector<std::string> path;
        for (std::size_t index = 1; index < expression.children.size(); ++index) {
            const auto* child = expression.children[index].get();
            if (!child || child->kind != AstKind::Identifier) {
                return false;
            }
            path.push_back(child->text);
        }
        const auto offset = offset_of(*type, path);
        if (!offset) {
            return false;
        }
        value = static_cast<long long>(*offset);
        return true;
    }
    case AstKind::CommaExpression:
        return expression.children.size() >= 2 && expression.children[1] &&
               evaluate_constant(*expression.children[1], value);
    default:
        return false;
    }
}

std::uint64_t Sema::size_of(const Type& type) const {
    const auto bytes = [](std::uint32_t bits) {
        return static_cast<std::uint64_t>(bits == 0 ? 1 : bits / 8);
    };
    switch (type.kind) {
    case TypeKind::Void:
        return 1;
    case TypeKind::Bool:
    case TypeKind::Char:
        return 1;
    case TypeKind::Short:
        return bytes(profile_.short_bits);
    case TypeKind::Int:
        return bytes(profile_.int_bits);
    case TypeKind::Long:
        return bytes(profile_.long_bits);
    case TypeKind::LongLong:
        return bytes(profile_.long_long_bits);
    case TypeKind::Float:
        return bytes(profile_.float_bits);
    case TypeKind::Double:
        return bytes(profile_.double_bits);
    case TypeKind::LongDouble:
        return bytes(profile_.long_double_bits);
    case TypeKind::Pointer:
        return bytes(profile_.pointer_bits);
    case TypeKind::Enum:
        return bytes(profile_.int_bits);
    case TypeKind::Array:
        if (!type.element || !type.array_has_count) {
            return 0;
        }
        return size_of(*type.element) * type.array_count;
    case TypeKind::Record: {
        if (!type.record || !type.record->complete) {
            return 0;
        }
        if (type.record->is_union) {
            std::uint64_t maximum = type.record->base ? size_of(*type.record->base) : 0;
            std::uint64_t alignment = type.record->base ? align_of(*type.record->base) : 1;
            for (const auto& member : type.record->members) {
                if (!member.type) {
                    continue;
                }
                maximum = std::max(maximum, size_of(*member.type));
                alignment = std::max(alignment, align_of(*member.type));
                alignment = std::max(alignment, member.alignment);
            }
            return round_up(maximum, alignment);
        }
        const auto offsets = member_offsets(*type.record);
        std::uint64_t end = type.record->base ? size_of(*type.record->base) : 0;
        if (!type.record->base && type.record->polymorphic) {
            end = 4;
        }
        for (std::size_t index = 0; index < type.record->members.size(); ++index) {
            const auto& member = type.record->members[index];
            if (member.type) {
                end = std::max(end, offsets[index] + size_of(*member.type));
            }
        }
        return round_up(end, align_of(type));
    }
    case TypeKind::Function:
        return 1;
    }
    return 1;
}

std::vector<std::uint64_t> Sema::member_offsets(const RecordInfo& record) const {
    std::vector<std::uint64_t> offsets(record.members.size(), 0);
    if (record.is_union) {
        return offsets;
    }

    std::uint64_t offset =
        record.base ? round_up(size_of(*record.base), align_of(*record.base))
                    : (record.polymorphic ? 4 : 0);
    std::uint64_t bit_used = 0;
    std::uint64_t unit_size = 0;
    std::uint64_t unit_start = 0;
    bool have_unit = false;

    for (std::size_t index = 0; index < record.members.size(); ++index) {
        const auto& member = record.members[index];
        if (!member.type) {
            continue;
        }
        const auto alignment = std::max(align_of(*member.type), member.alignment);
        if (member.bit_field) {
            if (member.bit_width == 0) {
                offset = round_up(offset, alignment);
                have_unit = false;
                offsets[index] = offset;
                continue;
            }
            const auto width = size_of(*member.type);
            if (!have_unit || unit_size != width || bit_used + member.bit_width > width * 8) {
                offset = round_up(offset, alignment);
                unit_size = width == 0 ? 1 : width;
                unit_start = offset;
                bit_used = 0;
                have_unit = true;
                offset += unit_size;
            }
            offsets[index] = unit_start;
            bit_used += member.bit_width;
            continue;
        }

        offset = round_up(offset, alignment);
        offsets[index] = offset;
        offset += size_of(*member.type);
        have_unit = false;
    }
    return offsets;
}

std::optional<std::uint64_t> Sema::offset_of(
    const Type& type, const std::vector<std::string>& path) const {
    if (path.empty() || type.kind != TypeKind::Record || !type.record ||
        !type.record->complete) {
        return std::nullopt;
    }
    const auto offsets = member_offsets(*type.record);
    for (std::size_t index = 0; index < type.record->members.size(); ++index) {
        const auto& member = type.record->members[index];
        if (member.name != path.front() || !member.type) {
            continue;
        }
        if (member.bit_field) {
            return std::nullopt;
        }
        if (path.size() == 1) {
            return offsets[index];
        }
        const std::vector<std::string> nested_path(path.begin() + 1, path.end());
        const auto nested = offset_of(*member.type, nested_path);
        if (nested) {
            return offsets[index] + *nested;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::uint64_t Sema::align_of(const Type& type) const {
    if (type.kind == TypeKind::Array && type.element) {
        return align_of(*type.element);
    }
    if (type.kind == TypeKind::Record) {
        if (!type.record || !type.record->complete) {
            return 1;
        }
        std::uint64_t alignment =
            type.record->base ? align_of(*type.record->base) : 1;
        if (!type.record->base && type.record->polymorphic) {
            alignment = std::max<std::uint64_t>(alignment, 4);
        }
        for (const auto& member : type.record->members) {
            if (member.type) {
                alignment = std::max(alignment, align_of(*member.type));
                alignment = std::max(alignment, member.alignment);
            }
        }
        return alignment;
    }
    if (type.kind == TypeKind::Function) {
        return 1;
    }
    return std::min<std::uint64_t>(size_of(type), 8);
}

}  // namespace krc::frontend
