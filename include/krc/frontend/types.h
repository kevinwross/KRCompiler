#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "krc/frontend/source_manager.h"

namespace krc::frontend {

enum class TypeKind {
    Void,
    Bool,
    Char,
    Short,
    Int,
    Long,
    LongLong,
    Float,
    Double,
    LongDouble,
    Pointer,
    Array,
    Function,
    Record,
    Enum,
};

struct Type;
using TypePtr = std::shared_ptr<const Type>;
struct AstNode;

enum class AccessLevel {
    Public,
    Private,
    Protected,
};

struct RecordMember {
    std::string name;
    TypePtr type;
    SourceLocation location;
    bool bit_field = false;
    unsigned bit_width = 0;
    std::uint64_t alignment = 0;
    const AstNode* default_initializer = nullptr;
    AccessLevel access = AccessLevel::Public;
};

struct RecordMethod {
    std::string name;
    TypePtr type;
    bool is_virtual = false;
    bool is_pure = false;
    bool is_deleted = false;
    AccessLevel access = AccessLevel::Public;
};

struct RecordInfo {
    std::string tag;
    bool is_union = false;
    bool complete = false;
    bool polymorphic = false;
    // `class` members are private until an access specifier says otherwise;
    // `struct` and `union` members are public.
    AccessLevel default_access = AccessLevel::Public;
    TypePtr base;
    std::vector<RecordMember> members;
    std::vector<RecordMethod> methods;
    // Names of functions declared `friend` in this record. Friend classes are
    // not recorded yet.
    std::vector<std::string> friends;
};

struct EnumInfo {
    std::string tag;
    bool complete = false;
    std::vector<std::pair<std::string, long long>> enumerators;
};

struct Type {
    TypeKind kind = TypeKind::Int;
    bool is_unsigned = false;
    bool is_const = false;
    bool is_volatile = false;
    bool is_reference = false;
    bool is_rvalue_reference = false;

    std::string name;

    TypePtr element;
    std::uint64_t array_count = 0;
    bool array_has_count = false;

    TypePtr return_type;
    std::vector<TypePtr> parameters;
    bool variadic = false;
    bool has_prototype = false;
    std::size_t default_argument_count = 0;

    std::shared_ptr<RecordInfo> record;
    std::shared_ptr<EnumInfo> enum_info;
};

[[nodiscard]] bool is_integer(const Type& type);
[[nodiscard]] bool is_arithmetic(const Type& type);
[[nodiscard]] bool is_scalar(const Type& type);
[[nodiscard]] bool is_pointer(const Type& type);
[[nodiscard]] bool is_array(const Type& type);
[[nodiscard]] bool is_function(const Type& type);
[[nodiscard]] bool is_record(const Type& type);
[[nodiscard]] bool is_void(const Type& type);
[[nodiscard]] std::string type_name(const Type& type);

}  // namespace krc::frontend
