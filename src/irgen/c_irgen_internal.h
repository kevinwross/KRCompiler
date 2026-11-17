#pragma once

// Helpers shared by the translation units that define CIRGen. They lived in the top of
// c_irgen.cpp, which made that file a single 10,800-line translation unit and the
// largest single cost in a build; splitting the definitions across files requires the
// helpers to be visible to all of them.
//
// The anonymous namespace is deliberate: each including translation unit gets its own
// copy, so the helpers keep internal linkage and the call sites stay unqualified.

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

namespace krc::irgen {
// A reference to a record names the record it refers to. Inline because every
// translation unit defining CIRGen includes this header.
inline frontend::TypePtr record_object_type(frontend::TypePtr type) {
    if (type && type->is_reference && type->element) {
        return type->element;
    }
    return type;
}


namespace {

inline bool is_function_type(const frontend::AstNode& node) {
    return node.kind == frontend::AstKind::FunctionDeclarator;
}

inline bool is_move_call(const frontend::AstNode& node) {
    return node.kind == frontend::AstKind::CallExpression && !node.children.empty() &&
           node.children.front() &&
           node.children.front()->kind == frontend::AstKind::Identifier &&
           (node.children.front()->text == "std::move" ||
            node.children.front()->text == "std::forward") && node.children.size() > 1 &&
           node.children[1];
}

// True when `type` is a pointer to a record holding `lanes` f32 values, the
// shape a mat4 (16 lanes) or vec4 (4 lanes) lowers to.
inline bool points_to_float_lane_record(ir::TypePtr type, std::size_t lanes) {
    return type && type->kind == ir::TypeKind::Pointer &&
           ir::is_float_lane_record(type->element, lanes);
}

// Maps a declared function to the tile intrinsic it names. Matching is by
// symbol and signature, so a user function that happens to share one of the
// kr32::matrix names keeps its own body and stays an ordinary call.
inline ir::Intrinsic matrix_intrinsic(const ir::Function& function) {
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
inline std::string type_symbol(const frontend::RecordInfo& record, std::string_view prefix) {
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

std::string mangle_method(std::string_view tag, std::string_view name);

// A method is keyed by its name, and by its constness only when the class
// declares that name twice: a real overload pair needs two entries, while a lone
// const method keeps the plain key so every existing lookup still finds it.
inline std::string method_key_of(std::string_view name, bool is_const) {
    return is_const ? std::string(name) + "#const" : std::string(name);
}

// Whether a function declarator carries `const`. The qualifier is on the
// FunctionDeclarator itself, so the walk stops there: a `const` inside a
// parameter's type belongs to that parameter, not to the method.
inline bool declarator_is_const(const frontend::AstNode& declarator) {
    if (declarator.kind == frontend::AstKind::FunctionDeclarator) {
        return std::find(declarator.labels.begin(), declarator.labels.end(),
                         "const") != declarator.labels.end();
    }
    for (const auto& child : declarator.children) {
        if (child && declarator_is_const(*child)) {
            return true;
        }
    }
    return false;
}

// The symbol a method is emitted under. The constness suffix is appended to the
// mangled name rather than passed into it: the mangler rewrites every character
// of an operator name, so "operator==__const" would spell `op_eqeqconst`.
inline std::string method_symbol(std::string_view tag, std::string_view name,
                          bool is_const) {
    auto symbol = mangle_method(tag, name);
    if (is_const) {
        symbol += "__const";
    }
    return symbol;
}
inline std::string mangle_method(std::string_view tag, std::string_view name) {
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

}  // namespace krc::irgen
