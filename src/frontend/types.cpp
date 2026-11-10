#include "krc/frontend/types.h"

#include <string>

namespace krc::frontend {

bool is_integer(const Type& type) {
    switch (type.kind) {
    case TypeKind::Bool:
    case TypeKind::Char:
    case TypeKind::Short:
    case TypeKind::Int:
    case TypeKind::Long:
    case TypeKind::LongLong:
    case TypeKind::Enum:
        return true;
    default:
        return false;
    }
}

bool is_arithmetic(const Type& type) {
    return is_integer(type) || type.kind == TypeKind::Float || type.kind == TypeKind::Double ||
           type.kind == TypeKind::LongDouble;
}

bool is_scalar(const Type& type) { return is_arithmetic(type) || is_pointer(type); }

bool is_pointer(const Type& type) { return type.kind == TypeKind::Pointer; }

bool is_array(const Type& type) { return type.kind == TypeKind::Array; }

bool is_function(const Type& type) { return type.kind == TypeKind::Function; }

bool is_record(const Type& type) { return type.kind == TypeKind::Record; }

bool is_void(const Type& type) { return type.kind == TypeKind::Void; }

std::string type_name(const Type& type) {
    switch (type.kind) {
    case TypeKind::Void:
        return "void";
    case TypeKind::Bool:
        return "_Bool";
    case TypeKind::Char:
        return type.is_unsigned ? "unsigned char" : "char";
    case TypeKind::Short:
        return type.is_unsigned ? "unsigned short" : "short";
    case TypeKind::Int:
        return type.is_unsigned ? "unsigned int" : "int";
    case TypeKind::Long:
        return type.is_unsigned ? "unsigned long" : "long";
    case TypeKind::LongLong:
        return type.is_unsigned ? "unsigned long long" : "long long";
    case TypeKind::Float:
        return "float";
    case TypeKind::Double:
        return "double";
    case TypeKind::LongDouble:
        return "long double";
    case TypeKind::Pointer:
        return type.element ? type_name(*type.element) + " *" : "void *";
    case TypeKind::Array:
        if (type.element) {
            if (type.array_has_count) {
                return type_name(*type.element) + "[" + std::to_string(type.array_count) + "]";
            }
            return type_name(*type.element) + "[]";
        }
        return "array";
    case TypeKind::Function:
        return type.return_type ? type_name(*type.return_type) + " (...)" : "function";
    case TypeKind::Record:
        return (type.record && type.record->is_union ? "union " : "struct ") +
               (type.record ? type.record->tag : std::string{});
    case TypeKind::Enum:
        return "enum " + (type.enum_info ? type.enum_info->tag : std::string{});
    }
    return "unknown";
}

}  // namespace krc::frontend
