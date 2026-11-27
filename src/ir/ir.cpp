#include "ir/ir.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "krc/target/target.h"

namespace krc::ir {

namespace {

std::string join_types(const std::vector<TypePtr>& types) {
    std::ostringstream out;
    for (std::size_t index = 0; index < types.size(); ++index) {
        if (index != 0) {
            out << ", ";
        }
        print_type(types[index], out);
    }
    return out.str();
}

std::string signed_constant(const ConstantInt& constant) {
    if (constant.type && constant.type->is_signed) {
        return std::to_string(constant.value);
    }
    return std::to_string(static_cast<std::uint64_t>(constant.value));
}

void print_value_raw(std::ostream& out, const Value* value);

void print_value(std::ostream& out, const Value* value) {
    if (!value) {
        out << "<null>";
        return;
    }
    switch (value->kind) {
    case ValueKind::Function:
    case ValueKind::Global:
        out << '@' << value->name;
        break;
    case ValueKind::Parameter:
    case ValueKind::Instruction:
        out << '%' << value->name;
        break;
    case ValueKind::ConstantInt:
        out << type_to_string(value->type) << ' '
            << signed_constant(*static_cast<const ConstantInt*>(value));
        break;
    case ValueKind::ConstantFloat: {
        const auto& constant = *static_cast<const ConstantFloat*>(value);
        out << type_to_string(value->type) << ' ' << std::setprecision(17) << constant.value;
        break;
    }
    case ValueKind::ConstantNull:
        out << "null";
        break;
    case ValueKind::ConstantUndef:
        out << "undef";
        break;
    case ValueKind::ConstantDataArray: {
        const auto& constant = *static_cast<const ConstantDataArray*>(value);
        out << "c\"";
        for (const auto byte : constant.bytes) {
            if (byte >= 0x20 && byte <= 0x7e && byte != '"' && byte != '\\') {
                out << static_cast<char>(byte);
            } else {
                constexpr char digits[] = "0123456789abcdef";
                out << '\\' << digits[(byte >> 4) & 0xf] << digits[byte & 0xf];
            }
        }
        out << '"';
        break;
    }
    case ValueKind::ConstantAddressArray: {
        const auto& constant = *static_cast<const ConstantAddressArray*>(value);
        out << '[';
        for (std::size_t index = 0; index < constant.elements.size(); ++index) {
            if (index != 0) {
                out << ", ";
            }
            print_value_raw(out, constant.elements[index]);
        }
        out << ']';
        break;
    }
    }
}

void print_value_raw(std::ostream& out, const Value* value) {
    if (!value) {
        out << "<null>";
        return;
    }
    switch (value->kind) {
    case ValueKind::ConstantInt:
        out << signed_constant(*static_cast<const ConstantInt*>(value));
        break;
    case ValueKind::ConstantFloat:
        out << std::setprecision(17)
            << static_cast<const ConstantFloat*>(value)->value;
        break;
    case ValueKind::ConstantNull:
        out << "null";
        break;
    case ValueKind::ConstantUndef:
        out << "undef";
        break;
    default:
        print_value(out, value);
        break;
    }
}

void print_block_label(std::ostream& out, const BasicBlock* block) {
    out << (block && !block->name.empty() ? block->name : "bb");
}

void print_debug(std::ostream& out, const DebugLocation& debug) {
    if (debug.valid()) {
        out << " ; " << debug.file << ':' << debug.line << ':' << debug.column;
    }
}

}  // namespace

TypePtr TypeContext::make(Type type) {
    types_.push_back(std::make_unique<Type>(std::move(type)));
    return types_.back().get();
}

TypePtr TypeContext::primitive(TypeKind kind, unsigned bits, bool is_signed) {
    Type type;
    type.kind = kind;
    type.bits = bits;
    type.is_signed = is_signed;
    return make(std::move(type));
}

TypePtr TypeContext::void_type() {
    if (!void_) {
        void_ = primitive(TypeKind::Void, 0, false);
    }
    return void_;
}

TypePtr TypeContext::bool_type() {
    if (!bool_) {
        bool_ = primitive(TypeKind::Integer, 1, false);
    }
    return bool_;
}

TypePtr TypeContext::int_type(unsigned bits, bool is_signed) {
    if (bits == 1) {
        return bool_type();
    }
    if (is_signed) {
        if (bits == 8) {
            if (!i8_) {
                i8_ = primitive(TypeKind::Integer, 8, true);
            }
            return i8_;
        }
        if (bits == 16) {
            if (!i16_) {
                i16_ = primitive(TypeKind::Integer, 16, true);
            }
            return i16_;
        }
        if (bits == 32) {
            if (!i32_) {
                i32_ = primitive(TypeKind::Integer, 32, true);
            }
            return i32_;
        }
        if (bits == 64) {
            if (!i64_) {
                i64_ = primitive(TypeKind::Integer, 64, true);
            }
            return i64_;
        }
    }
    return primitive(TypeKind::Integer, bits, is_signed);
}

TypePtr TypeContext::float_type(unsigned bits) {
    if (bits == 32) {
        if (!f32_) {
            f32_ = primitive(TypeKind::Float, 32, true);
        }
        return f32_;
    }
    if (bits == 64) {
        if (!f64_) {
            f64_ = primitive(TypeKind::Float, 64, true);
        }
        return f64_;
    }
    return primitive(TypeKind::Float, bits, true);
}

TypePtr TypeContext::pointer_type(TypePtr element) {
    // Cached so two `ptr<i32>` are the same address: the verifier compares type
    // pointers, so equality of shape must imply equality of pointer or IRGen has
    // to cast between values that already have the right type (ADR 0050).
    const auto found = pointers_.find(element);
    if (found != pointers_.end()) {
        return found->second;
    }
    Type type;
    type.kind = TypeKind::Pointer;
    type.element = element;
    const auto built = make(std::move(type));
    pointers_.emplace(element, built);
    return built;
}

TypePtr TypeContext::array_type(TypePtr element, std::uint64_t count) {
    const auto key = std::make_pair(element, count);
    const auto found = arrays_.find(key);
    if (found != arrays_.end()) {
        return found->second;
    }
    Type type;
    type.kind = TypeKind::Array;
    type.element = element;
    type.count = count;
    const auto built = make(std::move(type));
    arrays_.emplace(key, built);
    return built;
}

TypePtr TypeContext::struct_type(std::string name, std::vector<TypePtr> fields,
                                 std::vector<std::string> field_names, bool packed) {
    if (!name.empty()) {
        for (const auto* existing : named_types_) {
            if (existing->name == name) {
                return existing;
            }
        }
    }
    Type type;
    type.kind = TypeKind::Struct;
    type.name = std::move(name);
    type.fields = std::move(fields);
    type.field_names = std::move(field_names);
    type.packed = packed;
    const auto result = make(std::move(type));
    if (!result->name.empty()) {
        named_types_.push_back(result);
    }
    return result;
}

TypePtr TypeContext::struct_forward(std::string name) {
    if (!name.empty()) {
        for (const auto* existing : named_types_) {
            if (existing->kind == TypeKind::Struct && existing->name == name) {
                return existing;
            }
        }
    }
    Type type;
    type.kind = TypeKind::Struct;
    type.name = std::move(name);
    const auto result = make(std::move(type));
    if (!result->name.empty()) {
        named_types_.push_back(result);
    }
    return result;
}

bool TypeContext::complete_struct(TypePtr type, std::vector<TypePtr> fields,
                                  std::vector<std::string> field_names, bool packed,
                                  std::string base, TypePtr base_type) {
    if (!type || type->kind != TypeKind::Struct) {
        return false;
    }
    auto* mutable_type = const_cast<Type*>(type);
    if (!mutable_type->fields.empty()) {
        return false;
    }
    mutable_type->fields = std::move(fields);
    mutable_type->field_names = std::move(field_names);
    mutable_type->packed = packed;
    mutable_type->base = std::move(base);
    mutable_type->base_type = base_type;
    return true;
}

TypePtr TypeContext::union_type(std::string name, std::vector<TypePtr> fields,
                                std::vector<std::string> field_names) {
    if (!name.empty()) {
        for (const auto* existing : named_types_) {
            if (existing->name == name) {
                return existing;
            }
        }
    }
    Type type;
    type.kind = TypeKind::Union;
    type.name = std::move(name);
    type.fields = std::move(fields);
    type.field_names = std::move(field_names);
    const auto result = make(std::move(type));
    if (!result->name.empty()) {
        named_types_.push_back(result);
    }
    return result;
}

TypePtr TypeContext::function_type(TypePtr return_type, std::vector<TypePtr> parameters,
                                   bool variadic, bool unprototyped) {
    const auto key = std::make_tuple(return_type, parameters, variadic, unprototyped);
    const auto found = functions_.find(key);
    if (found != functions_.end()) {
        return found->second;
    }
    Type type;
    type.kind = TypeKind::Function;
    type.return_type = return_type;
    type.parameters = std::move(parameters);
    type.variadic = variadic;
    type.unprototyped = unprototyped;
    const auto built = make(std::move(type));
    functions_.emplace(key, built);
    return built;
}

bool same_type(TypePtr left, TypePtr right) {
    if (left == right) {
        return true;
    }
    if (!left || !right || left->kind != right->kind) {
        return false;
    }
    switch (left->kind) {
    case TypeKind::Void:
        return true;
    case TypeKind::Integer:
    case TypeKind::Float:
        return left->bits == right->bits && left->is_signed == right->is_signed;
    case TypeKind::Pointer:
        return same_type(left->element, right->element);
    case TypeKind::Array:
        return left->count == right->count && same_type(left->element, right->element);
    case TypeKind::Struct:
    case TypeKind::Union:
        if (!left->name.empty() || !right->name.empty()) {
            return left->name == right->name;
        }
        if (left->fields.size() != right->fields.size()) {
            return false;
        }
        for (std::size_t index = 0; index < left->fields.size(); ++index) {
            if (!same_type(left->fields[index], right->fields[index])) {
                return false;
            }
        }
        return left->packed == right->packed;
    case TypeKind::Function:
        if (left->variadic != right->variadic ||
            left->unprototyped != right->unprototyped ||
            !same_type(left->return_type, right->return_type) ||
            left->parameters.size() != right->parameters.size()) {
            return false;
        }
        for (std::size_t index = 0; index < left->parameters.size(); ++index) {
            if (!same_type(left->parameters[index], right->parameters[index])) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool is_integer(TypePtr type) { return type && type->kind == TypeKind::Integer; }
bool is_float(TypePtr type) { return type && type->kind == TypeKind::Float; }
bool is_pointer(TypePtr type) { return type && type->kind == TypeKind::Pointer; }
bool is_void(TypePtr type) { return type && type->kind == TypeKind::Void; }

bool is_float_lane_record(TypePtr type, std::size_t lanes) {
    if (!type || type->kind != TypeKind::Struct) {
        return false;
    }
    const auto is_lane = [](TypePtr field) {
        return field && is_float(field) && field->bits == 32;
    };
    if (type->fields.size() == 1 && type->fields[0] &&
        type->fields[0]->kind == TypeKind::Array && is_lane(type->fields[0]->element) &&
        type->fields[0]->count == lanes) {
        return true;
    }
    if (type->fields.size() != lanes) {
        return false;
    }
    for (const auto& field : type->fields) {
        if (!is_lane(field)) {
            return false;
        }
    }
    return true;
}

namespace {

bool is_i1_type(TypePtr type) { return is_integer(type) && type->bits == 1; }

}  // namespace

std::string type_to_string(TypePtr type) {
    if (!type) {
        return "<null>";
    }
    switch (type->kind) {
    case TypeKind::Void:
        return "void";
    case TypeKind::Integer:
        return "i" + std::to_string(type->bits);
    case TypeKind::Float:
        return "f" + std::to_string(type->bits);
    case TypeKind::Pointer:
        return "ptr<" + type_to_string(type->element) + ">";
    case TypeKind::Array:
        return "[" + std::to_string(type->count) + " x " + type_to_string(type->element) + "]";
    case TypeKind::Struct:
        if (!type->name.empty()) {
            return "%" + type->name;
        }
        return "{" + join_types(type->fields) + "}";
    case TypeKind::Union:
        if (!type->name.empty()) {
            return "%" + type->name;
        }
        return "<{" + join_types(type->fields) + "}>";
    case TypeKind::Function:
        if (type->unprototyped) {
            return "fn(...) -> " + type_to_string(type->return_type);
        }
        return "fn(" + join_types(type->parameters) + ")" +
               (type->variadic ? ", ..." : "") + " -> " + type_to_string(type->return_type);
    }
    return "<unknown>";
}

bool is_terminator(Opcode opcode) {
    switch (opcode) {
    case Opcode::Br:
    case Opcode::CondBr:
    case Opcode::Switch:
    case Opcode::Ret:
    case Opcode::Unreachable:
    case Opcode::Invoke:
    case Opcode::Resume:
        return true;
    default:
        return false;
    }
}

std::string_view opcode_name(Opcode opcode) {
    switch (opcode) {
    case Opcode::Add:
        return "add";
    case Opcode::Sub:
        return "sub";
    case Opcode::Mul:
        return "mul";
    case Opcode::UDiv:
        return "udiv";
    case Opcode::SDiv:
        return "sdiv";
    case Opcode::URem:
        return "urem";
    case Opcode::SRem:
        return "srem";
    case Opcode::FAdd:
        return "fadd";
    case Opcode::FSub:
        return "fsub";
    case Opcode::FMul:
        return "fmul";
    case Opcode::FDiv:
        return "fdiv";
    case Opcode::FRem:
        return "frem";
    case Opcode::Shl:
        return "shl";
    case Opcode::LShr:
        return "lshr";
    case Opcode::AShr:
        return "ashr";
    case Opcode::And:
        return "and";
    case Opcode::Or:
        return "or";
    case Opcode::Xor:
        return "xor";
    case Opcode::Trunc:
        return "trunc";
    case Opcode::ZExt:
        return "zext";
    case Opcode::SExt:
        return "sext";
    case Opcode::FPTrunc:
        return "fptrunc";
    case Opcode::FPExt:
        return "fpext";
    case Opcode::FPToUI:
        return "fptoui";
    case Opcode::FPToSI:
        return "fptosi";
    case Opcode::UIToFP:
        return "uitofp";
    case Opcode::SIToFP:
        return "sitofp";
    case Opcode::BitCast:
        return "bitcast";
    case Opcode::PtrToInt:
        return "ptrtoint";
    case Opcode::IntToPtr:
        return "inttoptr";
    case Opcode::ICmp:
        return "icmp";
    case Opcode::FCmp:
        return "fcmp";
    case Opcode::Load:
        return "load";
    case Opcode::Store:
        return "store";
    case Opcode::Alloca:
        return "alloca";
    case Opcode::VaStart:
        return "vastart";
    case Opcode::GetElementPtr:
        return "getelementptr";
    case Opcode::Memcpy:
        return "memcpy";
    case Opcode::Memset:
        return "memset";
    case Opcode::Call:
        return "call";
    case Opcode::Br:
        return "br";
    case Opcode::CondBr:
        return "condbr";
    case Opcode::Switch:
        return "switch";
    case Opcode::Ret:
        return "ret";
    case Opcode::Unreachable:
        return "unreachable";
    case Opcode::Phi:
        return "phi";
    case Opcode::Select:
        return "select";
    case Opcode::ExtractValue:
        return "extractvalue";
    case Opcode::InsertValue:
        return "insertvalue";
    case Opcode::Invoke:
        return "invoke";
    case Opcode::LandingPad:
        return "landingpad";
    case Opcode::Resume:
        return "resume";
    case Opcode::Intrinsic:
        return "intrinsic";
    }
    return "unknown";
}

std::string_view intrinsic_name(Intrinsic intrinsic) {
    switch (intrinsic) {
    case Intrinsic::None:
        return "none";
    case Intrinsic::MatrixMultiply:
        return "matrix.multiply";
    case Intrinsic::MatrixMultiplyAdd:
        return "matrix.multiply_add";
    case Intrinsic::MatrixTranspose:
        return "matrix.transpose";
    case Intrinsic::MatrixVectorTransform:
        return "matrix.transform";
    case Intrinsic::VectorDot:
        return "vector.dot";
    case Intrinsic::TransformPush:
        return "transform.push";
    case Intrinsic::TransformPop:
        return "transform.pop";
    case Intrinsic::TransformDrop:
        return "transform.drop";
    }
    return "none";
}

std::string_view icmp_predicate_name(ICmpPredicate predicate) {
    switch (predicate) {
    case ICmpPredicate::Eq:
        return "eq";
    case ICmpPredicate::Ne:
        return "ne";
    case ICmpPredicate::Ugt:
        return "ugt";
    case ICmpPredicate::Uge:
        return "uge";
    case ICmpPredicate::Ult:
        return "ult";
    case ICmpPredicate::Ule:
        return "ule";
    case ICmpPredicate::Sgt:
        return "sgt";
    case ICmpPredicate::Sge:
        return "sge";
    case ICmpPredicate::Slt:
        return "slt";
    case ICmpPredicate::Sle:
        return "sle";
    }
    return "unknown";
}

std::string_view fcmp_predicate_name(FCmpPredicate predicate) {
    switch (predicate) {
    case FCmpPredicate::False:
        return "false";
    case FCmpPredicate::Oeq:
        return "oeq";
    case FCmpPredicate::Ogt:
        return "ogt";
    case FCmpPredicate::Oge:
        return "oge";
    case FCmpPredicate::Olt:
        return "olt";
    case FCmpPredicate::Ole:
        return "ole";
    case FCmpPredicate::One:
        return "one";
    case FCmpPredicate::Ord:
        return "ord";
    case FCmpPredicate::Uno:
        return "uno";
    case FCmpPredicate::True:
        return "true";
    }
    return "unknown";
}

Parameter::Parameter(TypePtr parameter_type, std::string parameter_name)
    : Value(ValueKind::Parameter, parameter_type, std::move(parameter_name)) {}

Function::Function(TypePtr function_type, std::string function_name)
    : Value(ValueKind::Function, function_type, std::move(function_name)) {}

Instruction* BasicBlock::terminator() const {
    if (instructions.empty()) {
        return nullptr;
    }
    auto* last = instructions.back().get();
    return is_terminator(last->opcode) ? last : nullptr;
}

Parameter* Function::add_parameter(TypePtr parameter_type, std::string parameter_name) {
    auto parameter = std::make_unique<Parameter>(parameter_type, std::move(parameter_name));
    auto* result = parameter.get();
    parameters.push_back(std::move(parameter));
    return result;
}

BasicBlock* Function::add_block(std::string block_name) {
    if (block_name.empty()) {
        block_name = "bb" + std::to_string(blocks.size());
    }
    const auto base = block_name;
    std::size_t suffix = 1;
    while (std::any_of(blocks.begin(), blocks.end(), [&](const auto& block) {
        return block->name == block_name;
    })) {
        block_name = base + "." + std::to_string(suffix++);
    }
    auto block = std::make_unique<BasicBlock>();
    block->name = std::move(block_name);
    block->parent = this;
    auto* result = block.get();
    blocks.push_back(std::move(block));
    return result;
}

BasicBlock* Function::entry_block() const { return blocks.empty() ? nullptr : blocks.front().get(); }

BasicBlock* Function::find_block(std::string_view block_name) const {
    for (const auto& block : blocks) {
        if (block->name == block_name) {
            return block.get();
        }
    }
    return nullptr;
}

Module::Module(target::TargetId target) : target_(target) {}Function* Module::add_function(std::string name, TypePtr function_type, Linkage linkage,
                               bool declaration) {
    auto function = std::make_unique<Function>(function_type, std::move(name));
    function->linkage = linkage;
    function->declaration = declaration;
    auto* result = function.get();
    functions_.push_back(std::move(function));
    return result;
}

GlobalVariable* Module::add_global(std::string name, TypePtr element_type, bool constant,
                                   std::uint32_t alignment) {
    auto global = std::make_unique<GlobalVariable>();
    global->kind = ValueKind::Global;
    global->type = types_.pointer_type(element_type);
    global->element_type = element_type;
    global->name = std::move(name);
    global->constant = constant;
    global->alignment = alignment;
    auto* result = global.get();
    globals_.push_back(std::move(global));
    return result;
}

ConstantInt* Module::constant_int(TypePtr type, std::int64_t value) {
    auto constant = std::make_unique<ConstantInt>();
    constant->kind = ValueKind::ConstantInt;
    constant->type = type;
    constant->value = value;
    auto* result = constant.get();
    constants_.push_back(std::move(constant));
    return result;
}

ConstantFloat* Module::constant_float(TypePtr type, double value) {
    auto constant = std::make_unique<ConstantFloat>();
    constant->kind = ValueKind::ConstantFloat;
    constant->type = type;
    constant->value = value;
    auto* result = constant.get();
    constants_.push_back(std::move(constant));
    return result;
}

ConstantNull* Module::constant_null(TypePtr type) {
    auto constant = std::make_unique<ConstantNull>();
    constant->kind = ValueKind::ConstantNull;
    constant->type = type;
    auto* result = constant.get();
    constants_.push_back(std::move(constant));
    return result;
}

ConstantUndef* Module::constant_undef(TypePtr type) {
    auto constant = std::make_unique<ConstantUndef>();
    constant->kind = ValueKind::ConstantUndef;
    constant->type = type;
    auto* result = constant.get();
    constants_.push_back(std::move(constant));
    return result;
}

ConstantDataArray* Module::constant_data_array(TypePtr array_type,
                                               std::vector<std::uint8_t> bytes) {
    auto constant = std::make_unique<ConstantDataArray>();
    constant->kind = ValueKind::ConstantDataArray;
    constant->type = array_type;
    constant->bytes = std::move(bytes);
    auto* result = constant.get();
    constants_.push_back(std::move(constant));
    return result;
}

ConstantAddressArray* Module::constant_address_array(TypePtr array_type,
                                                     std::vector<Value*> elements) {
    auto constant = std::make_unique<ConstantAddressArray>();
    constant->kind = ValueKind::ConstantAddressArray;
    constant->type = array_type;
    constant->elements = std::move(elements);
    auto* result = constant.get();
    constants_.push_back(std::move(constant));
    return result;
}

void IRBuilder::set_insert_point(BasicBlock* block) { block_ = block; }

void IRBuilder::set_debug_location(DebugLocation location) {
    current_debug_ = std::move(location);
}

void IRBuilder::reset_value_names() {
    value_names_.clear();
    temp_counter_ = 0;
}

IRBuilder::IRBuilder(TypeContext& types) : types_(types) {}

std::string IRBuilder::make_name(std::string name) {
    if (name.empty()) {
        name = "tmp" + std::to_string(temp_counter_++);
    }
    if (value_names_.insert(name).second) {
        return name;
    }
    std::string base = name;
    do {
        name = base + "." + std::to_string(temp_counter_++);
    } while (!value_names_.insert(name).second);
    return name;
}

Instruction* IRBuilder::append(Opcode opcode, TypePtr type, std::vector<Value*> operands,
                               std::string name) {
    assert(block_ != nullptr && "IRBuilder has no insertion point");
    assert(!block_->terminated() && "cannot append after a terminator");
    auto instruction = std::make_unique<Instruction>();
    instruction->kind = ValueKind::Instruction;
    instruction->type = type;
    instruction->name = make_name(std::move(name));
    instruction->opcode = opcode;
    instruction->parent = block_;
    instruction->operands = std::move(operands);
    auto* result = instruction.get();
    block_->instructions.push_back(std::move(instruction));
    return result;
}

Instruction* IRBuilder::create(Opcode opcode, TypePtr type, std::vector<Value*> operands,
                               std::string name) {
    auto* instruction = append(opcode, type, std::move(operands), std::move(name));
    instruction->debug = current_debug_;
    return instruction;
}

Instruction* IRBuilder::create_add(Value* left, Value* right, std::string name) {
    return create(Opcode::Add, left ? left->type : nullptr, {left, right}, std::move(name));
}

Instruction* IRBuilder::create_sub(Value* left, Value* right, std::string name) {
    return create(Opcode::Sub, left ? left->type : nullptr, {left, right}, std::move(name));
}

Instruction* IRBuilder::create_mul(Value* left, Value* right, std::string name) {
    return create(Opcode::Mul, left ? left->type : nullptr, {left, right}, std::move(name));
}

Instruction* IRBuilder::create_sdiv(Value* left, Value* right, std::string name) {
    return create(Opcode::SDiv, left ? left->type : nullptr, {left, right}, std::move(name));
}

Instruction* IRBuilder::create_udiv(Value* left, Value* right, std::string name) {
    return create(Opcode::UDiv, left ? left->type : nullptr, {left, right}, std::move(name));
}

Instruction* IRBuilder::create_srem(Value* left, Value* right, std::string name) {
    return create(Opcode::SRem, left ? left->type : nullptr, {left, right}, std::move(name));
}

Instruction* IRBuilder::create_and(Value* left, Value* right, std::string name) {
    return create(Opcode::And, left ? left->type : nullptr, {left, right}, std::move(name));
}

Instruction* IRBuilder::create_or(Value* left, Value* right, std::string name) {
    return create(Opcode::Or, left ? left->type : nullptr, {left, right}, std::move(name));
}

Instruction* IRBuilder::create_xor(Value* left, Value* right, std::string name) {
    return create(Opcode::Xor, left ? left->type : nullptr, {left, right}, std::move(name));
}

Instruction* IRBuilder::create_shl(Value* left, Value* right, std::string name) {
    return create(Opcode::Shl, left ? left->type : nullptr, {left, right}, std::move(name));
}

Instruction* IRBuilder::create_ashr(Value* left, Value* right, std::string name) {
    return create(Opcode::AShr, left ? left->type : nullptr, {left, right}, std::move(name));
}

Instruction* IRBuilder::create_lshr(Value* left, Value* right, std::string name) {
    return create(Opcode::LShr, left ? left->type : nullptr, {left, right}, std::move(name));
}

Instruction* IRBuilder::create_icmp(ICmpPredicate predicate, Value* left, Value* right,
                                    std::string name) {
    auto* instruction =
        create(Opcode::ICmp, types_.bool_type(), {left, right}, std::move(name));
    instruction->icmp_predicate = predicate;
    return instruction;
}

Instruction* IRBuilder::create_fcmp(FCmpPredicate predicate, Value* left, Value* right,
                                    std::string name) {
    auto* instruction =
        create(Opcode::FCmp, types_.bool_type(), {left, right}, std::move(name));
    instruction->fcmp_predicate = predicate;
    return instruction;
}

Instruction* IRBuilder::create_load(TypePtr result_type, Value* pointer, bool is_volatile,
                                    std::uint32_t alignment, std::string name) {
    auto* instruction =
        create(Opcode::Load, result_type, {pointer}, std::move(name));
    instruction->is_volatile = is_volatile;
    instruction->alignment = alignment;
    return instruction;
}

Instruction* IRBuilder::create_store(Value* value, Value* pointer, bool is_volatile,
                                     std::uint32_t alignment) {
    auto* instruction = create(Opcode::Store, types_.void_type(), {value, pointer});
    instruction->is_volatile = is_volatile;
    instruction->alignment = alignment;
    return instruction;
}

Instruction* IRBuilder::create_alloca(TypePtr allocated_type, std::uint32_t alignment,
                                      std::string name) {
    auto* instruction = create(Opcode::Alloca, types_.pointer_type(allocated_type), {},
                               std::move(name));
    instruction->allocated_type = allocated_type;
    instruction->alignment = alignment;
    return instruction;
}

Instruction* IRBuilder::create_va_start(std::string name) {
    return create(Opcode::VaStart, types_.pointer_type(types_.int_type(8, false)), {},
                  std::move(name));
}

Instruction* IRBuilder::create_gep(TypePtr source_type, Value* base,
                                   std::vector<Value*> indices, bool inbounds,
                                   std::string name) {
    std::vector<Value*> operands;
    operands.push_back(base);
    operands.insert(operands.end(), indices.begin(), indices.end());
    auto* instruction = create(Opcode::GetElementPtr, types_.pointer_type(source_type),
                               std::move(operands), std::move(name));
    instruction->source_type = source_type;
    instruction->inbounds = inbounds;
    return instruction;
}

Instruction* IRBuilder::create_call(Value* callee, std::vector<Value*> arguments,
                                    TypePtr result_type, std::string name) {
    TypePtr function_type = callee ? callee->type : nullptr;
    if (function_type && function_type->kind == TypeKind::Pointer) {
        function_type = function_type->element;
    }
    if (!result_type && function_type && function_type->kind == TypeKind::Function) {
        result_type = function_type->return_type;
    }
    std::vector<Value*> operands;
    operands.push_back(callee);
    operands.insert(operands.end(), arguments.begin(), arguments.end());
    return create(Opcode::Call, result_type, std::move(operands), std::move(name));
}

Instruction* IRBuilder::create_intrinsic(Intrinsic intrinsic, TypePtr result_type,
                                         std::vector<Value*> arguments, std::string name) {
    auto* instruction =
        create(Opcode::Intrinsic, std::move(result_type), std::move(arguments),
               std::move(name));
    instruction->intrinsic = intrinsic;
    return instruction;
}

Instruction* IRBuilder::create_invoke(Value* callee, std::vector<Value*> arguments,
                                      TypePtr result_type, BasicBlock* normal,
                                      BasicBlock* unwind, std::string name) {
    TypePtr function_type = callee ? callee->type : nullptr;
    if (function_type && function_type->kind == TypeKind::Pointer) {
        function_type = function_type->element;
    }
    if (!result_type && function_type && function_type->kind == TypeKind::Function) {
        result_type = function_type->return_type;
    }
    std::vector<Value*> operands;
    operands.push_back(callee);
    operands.insert(operands.end(), arguments.begin(), arguments.end());
    auto* instruction = create(Opcode::Invoke, std::move(result_type),
                               std::move(operands), std::move(name));
    instruction->successors.push_back(normal);
    instruction->successors.push_back(unwind);
    return instruction;
}

Instruction* IRBuilder::create_landingpad(std::string name) {
    const auto pointer = types_.pointer_type(types_.int_type(8));
    return create(Opcode::LandingPad, pointer, {}, std::move(name));
}

Instruction* IRBuilder::create_resume(Value* exception) {
    return create(Opcode::Resume, types_.void_type(), {exception});
}

Instruction* IRBuilder::create_br(BasicBlock* destination) {
    auto* instruction = create(Opcode::Br, types_.void_type(), {});
    instruction->successors.push_back(destination);
    return instruction;
}

Instruction* IRBuilder::create_cond_br(Value* condition, BasicBlock* when_true,
                                       BasicBlock* when_false) {
    auto* instruction = create(Opcode::CondBr, types_.void_type(), {condition});
    instruction->successors.push_back(when_true);
    instruction->successors.push_back(when_false);
    return instruction;
}

Instruction* IRBuilder::create_switch(Value* condition, BasicBlock* default_destination) {
    auto* instruction = create(Opcode::Switch, types_.void_type(), {condition});
    instruction->default_successor = default_destination;
    instruction->successors.push_back(default_destination);
    return instruction;
}

Instruction* IRBuilder::create_ret(Value* value) {
    std::vector<Value*> operands;
    if (value) {
        operands.push_back(value);
    }
    return create(Opcode::Ret, types_.void_type(), std::move(operands));
}

Instruction* IRBuilder::create_unreachable() {
    return create(Opcode::Unreachable, types_.void_type(), {});
}

Instruction* IRBuilder::create_phi(TypePtr type, std::string name) {
    return create(Opcode::Phi, type, {}, std::move(name));
}

Instruction* IRBuilder::create_select(Value* condition, Value* when_true, Value* when_false,
                                      std::string name) {
    return create(Opcode::Select, when_true ? when_true->type : nullptr,
                  {condition, when_true, when_false}, std::move(name));
}

Instruction* IRBuilder::create_extract_value(Value* aggregate,
                                             std::vector<unsigned> indices,
                                             TypePtr result_type, std::string name) {
    auto* instruction =
        create(Opcode::ExtractValue, result_type, {aggregate}, std::move(name));
    instruction->aggregate_indices = std::move(indices);
    return instruction;
}

Instruction* IRBuilder::create_insert_value(Value* aggregate, Value* inserted,
                                            std::vector<unsigned> indices,
                                            std::string name) {
    auto* instruction = create(Opcode::InsertValue, aggregate ? aggregate->type : nullptr,
                               {aggregate, inserted}, std::move(name));
    instruction->aggregate_indices = std::move(indices);
    return instruction;
}

void print_type(TypePtr type, std::ostream& out) { out << type_to_string(type); }

namespace {

void print_instruction(std::ostream& out, const Instruction& instruction) {
    const bool has_result =
        instruction.type && !is_void(instruction.type) && !instruction.name.empty();
    out << "  ";
    if (has_result) {
        out << '%' << instruction.name << " = ";
    }

    const auto operand = [&](std::size_t index) -> const Value* {
        return index < instruction.operands.size() ? instruction.operands[index] : nullptr;
    };
    const auto print_operand = [&](std::size_t index) {
        const auto* value = operand(index);
        if (value) {
            print_value_raw(out, value);
        } else {
            out << "<missing>";
        }
    };
    const auto print_flags = [&]() {
        if (instruction.alignment != 0) {
            out << ", align " << instruction.alignment;
        }
        if (instruction.is_volatile) {
            out << ", volatile";
        }
    };

    switch (instruction.opcode) {
    case Opcode::Add:
    case Opcode::Sub:
    case Opcode::Mul:
    case Opcode::UDiv:
    case Opcode::SDiv:
    case Opcode::URem:
    case Opcode::SRem:
    case Opcode::FAdd:
    case Opcode::FSub:
    case Opcode::FMul:
    case Opcode::FDiv:
    case Opcode::FRem:
    case Opcode::Shl:
    case Opcode::LShr:
    case Opcode::AShr:
    case Opcode::And:
    case Opcode::Or:
    case Opcode::Xor:
        out << opcode_name(instruction.opcode) << ' ' << type_to_string(instruction.type) << ' ';
        print_operand(0);
        out << ", ";
        print_operand(1);
        break;
    case Opcode::Trunc:
    case Opcode::ZExt:
    case Opcode::SExt:
    case Opcode::FPTrunc:
    case Opcode::FPExt:
    case Opcode::FPToUI:
    case Opcode::FPToSI:
    case Opcode::UIToFP:
    case Opcode::SIToFP:
    case Opcode::BitCast:
    case Opcode::PtrToInt:
    case Opcode::IntToPtr:
        out << opcode_name(instruction.opcode) << ' ';
        if (operand(0) && operand(0)->type) {
            out << type_to_string(operand(0)->type) << ' ';
        }
        print_operand(0);
        out << " to " << type_to_string(instruction.type);
        break;
    case Opcode::ICmp:
        out << "icmp " << icmp_predicate_name(instruction.icmp_predicate) << ' '
            << (operand(0) ? type_to_string(operand(0)->type) : std::string{"?"}) << ' ';
        print_operand(0);
        out << ", ";
        print_operand(1);
        break;
    case Opcode::FCmp:
        out << "fcmp " << fcmp_predicate_name(instruction.fcmp_predicate) << ' '
            << (operand(0) ? type_to_string(operand(0)->type) : std::string{"?"}) << ' ';
        print_operand(0);
        out << ", ";
        print_operand(1);
        break;
    case Opcode::Load:
        out << "load " << type_to_string(instruction.type) << ", ";
        print_operand(0);
        print_flags();
        break;
    case Opcode::Store:
        out << "store ";
        if (operand(0) && operand(0)->type) {
            out << type_to_string(operand(0)->type) << ' ';
        }
        print_operand(0);
        out << ", ";
        print_operand(1);
        print_flags();
        break;
    case Opcode::Alloca:
        out << "alloca " << type_to_string(instruction.allocated_type);
        if (instruction.alignment != 0) {
            out << ", align " << instruction.alignment;
        }
        break;
    case Opcode::VaStart:
        out << "vastart";
        break;
    case Opcode::GetElementPtr:
        out << "getelementptr " << (instruction.inbounds ? "inbounds " : "")
            << type_to_string(instruction.source_type) << ", ";
        for (std::size_t index = 0; index < instruction.operands.size(); ++index) {
            if (index != 0) {
                out << ", ";
            }
            print_operand(index);
        }
        break;
    case Opcode::Memcpy:
        out << "memcpy ";
        print_operand(0);
        out << ", ";
        print_operand(1);
        out << ", ";
        print_operand(2);
        print_flags();
        break;
    case Opcode::Memset:
        out << "memset ";
        print_operand(0);
        out << ", ";
        print_operand(1);
        out << ", ";
        print_operand(2);
        print_flags();
        break;
    case Opcode::Call:
        out << "call " << type_to_string(instruction.type) << ' ';
        print_operand(0);
        out << '(';
        for (std::size_t index = 1; index < instruction.operands.size(); ++index) {
            if (index != 1) {
                out << ", ";
            }
            if (operand(index) && operand(index)->type) {
                out << type_to_string(operand(index)->type) << ' ';
            }
            print_operand(index);
        }
        out << ')';
        break;
    case Opcode::Intrinsic:
        out << "intrinsic " << type_to_string(instruction.type) << ' '
            << intrinsic_name(instruction.intrinsic) << '(';
        for (std::size_t index = 0; index < instruction.operands.size(); ++index) {
            if (index != 0) {
                out << ", ";
            }
            if (operand(index) && operand(index)->type) {
                out << type_to_string(operand(index)->type) << ' ';
            }
            print_operand(index);
        }
        out << ')';
        break;
    case Opcode::Br:
        out << "br label %";
        if (!instruction.successors.empty() && instruction.successors.front()) {
            print_block_label(out, instruction.successors.front());
        }
        break;
    case Opcode::CondBr:
        out << "br ";
        if (operand(0) && operand(0)->type) {
            out << type_to_string(operand(0)->type) << ' ';
        }
        print_operand(0);
        out << ", label %";
        if (!instruction.successors.empty() && instruction.successors[0]) {
            print_block_label(out, instruction.successors[0]);
        }
        out << ", label %";
        if (instruction.successors.size() > 1 && instruction.successors[1]) {
            print_block_label(out, instruction.successors[1]);
        }
        break;
    case Opcode::Switch:
        out << "switch ";
        if (operand(0) && operand(0)->type) {
            out << type_to_string(operand(0)->type) << ' ';
        }
        print_operand(0);
        out << ", label %";
        print_block_label(out, instruction.default_successor);
        out << " [";
        for (const auto& switch_case : instruction.switch_cases) {
            out << ' ';
            if (switch_case.first) {
                print_value(out, switch_case.first);
            }
            out << ", label %";
            print_block_label(out, switch_case.second);
        }
        out << " ]";
        break;
    case Opcode::Ret:
        out << "ret ";
        if (instruction.operands.empty()) {
            out << "void";
        } else {
            if (operand(0) && operand(0)->type) {
                out << type_to_string(operand(0)->type) << ' ';
            }
            print_operand(0);
        }
        break;
    case Opcode::Unreachable:
        out << "unreachable";
        break;
    case Opcode::Phi:
        out << "phi " << type_to_string(instruction.type);
        for (const auto& incoming : instruction.incoming) {
            out << " [ ";
            print_value(out, incoming.first);
            out << ", %";
            print_block_label(out, incoming.second);
            out << " ]";
        }
        break;
    case Opcode::Select:
        out << "select ";
        if (operand(0) && operand(0)->type) {
            out << type_to_string(operand(0)->type) << ' ';
        }
        print_operand(0);
        out << ", ";
        print_operand(1);
        out << ", ";
        print_operand(2);
        break;
    case Opcode::ExtractValue:
        out << "extractvalue ";
        if (operand(0) && operand(0)->type) {
            out << type_to_string(operand(0)->type) << ' ';
        }
        print_operand(0);
        for (const auto index : instruction.aggregate_indices) {
            out << ", " << index;
        }
        break;
    case Opcode::InsertValue:
        out << "insertvalue ";
        if (operand(0) && operand(0)->type) {
            out << type_to_string(operand(0)->type) << ' ';
        }
        print_operand(0);
        out << ", ";
        print_operand(1);
        for (const auto index : instruction.aggregate_indices) {
            out << ", " << index;
        }
        break;
    case Opcode::Invoke:
        out << "invoke " << type_to_string(instruction.type) << ' ';
        print_operand(0);
        out << '(';
        for (std::size_t index = 1; index < instruction.operands.size(); ++index) {
            if (index != 1) {
                out << ", ";
            }
            if (operand(index) && operand(index)->type) {
                out << type_to_string(operand(index)->type) << ' ';
            }
            print_operand(index);
        }
        out << ") to label %";
        if (!instruction.successors.empty() && instruction.successors[0]) {
            print_block_label(out, instruction.successors[0]);
        }
        out << " unwind label %";
        if (instruction.successors.size() > 1 && instruction.successors[1]) {
            print_block_label(out, instruction.successors[1]);
        }
        break;
    case Opcode::LandingPad:
        out << "landingpad " << type_to_string(instruction.type);
        break;
    case Opcode::Resume:
        out << "resume ";
        print_operand(0);
        break;
    }
    print_debug(out, instruction.debug);
    out << '\n';
}

}  // namespace

void print_function(const Function& function, std::ostream& out) {
    if (!function.type || function.type->kind != TypeKind::Function) {
        out << "; invalid function @" << function.name << '\n';
        return;
    }
    if (function.declaration) {
        out << "declare " << type_to_string(function.type->return_type) << " @"
            << function.name << '(';
        for (std::size_t index = 0; index < function.parameters.size(); ++index) {
            if (index != 0) {
                out << ", ";
            }
            out << type_to_string(function.parameters[index]->type);
        }
        if (function.type->variadic) {
            if (!function.parameters.empty()) {
                out << ", ";
            }
            out << "...";
        }
        out << ")\n";
        return;
    }
    out << "define ";
    if (function.linkage == Linkage::Internal) {
        out << "internal ";
    } else if (function.linkage == Linkage::Weak) {
        out << "weak ";
    }
    out << type_to_string(function.type->return_type) << " @" << function.name << '(';
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        if (index != 0) {
            out << ", ";
        }
        const auto& parameter = function.parameters[index];
        out << type_to_string(parameter->type) << " %" << parameter->name;
    }
    out << ") {\n";
    for (const auto& block : function.blocks) {
        out << block->name << ":\n";
        for (const auto& instruction : block->instructions) {
            print_instruction(out, *instruction);
        }
    }
    out << "}\n";
}

void print_module(const Module& module, std::ostream& out) {
    out << "; target: " << krc::target::to_string(module.target()) << '\n';
    for (const auto* type : module.types().named_types()) {
        if (type->kind != TypeKind::Struct && type->kind != TypeKind::Union) {
            continue;
        }
        out << '%' << type->name << " = type ";
        const char open = type->kind == TypeKind::Struct ? '{' : '<';
        const char close = type->kind == TypeKind::Struct ? '}' : '>';
        out << open;
        for (std::size_t index = 0; index < type->fields.size(); ++index) {
            if (index != 0) {
                out << ", ";
            }
            print_type(type->fields[index], out);
        }
        out << close << '\n';
    }
    for (const auto& global : module.globals()) {
        out << '@' << global->name << " = "
            << (global->external ? "external " : "")
            << (global->internal ? "internal " : "")
            << (global->weak ? "weak " : "")
            << (global->constant ? "constant " : "global ")
            << type_to_string(global->element_type) << ' ';
        if (global->initializer) {
            print_value_raw(out, global->initializer);
        } else {
            out << "zeroinitializer";
        }
        if (global->alignment != 0) {
            out << ", align " << global->alignment;
        }
        out << '\n';
    }
    for (const auto& function : module.functions()) {
        print_function(*function, out);
    }
}

bool Verifier::verify(const Module& module, std::string& error) const {
    const auto fail = [&](std::string message) {
        error = std::move(message);
        return false;
    };

    for (const auto* type : module.types().named_types()) {
        for (const auto* field : type->fields) {
            if (!field) {
                return fail("type '" + type->name + "' has a null field");
            }
        }
    }
    for (const auto& constant : module.constants()) {
        if (!constant->type) {
            return fail("constant has no type");
        }
        if (constant->kind == ValueKind::ConstantInt && !is_integer(constant->type)) {
            return fail("integer constant has a non-integer type");
        }
        if (constant->kind == ValueKind::ConstantFloat && !is_float(constant->type)) {
            return fail("floating constant has a non-floating type");
        }
        if (constant->kind == ValueKind::ConstantDataArray) {
            const auto& data = *static_cast<const ConstantDataArray*>(constant.get());
            const bool integer_elements =
                constant->type && constant->type->element &&
                is_integer(constant->type->element);
            const auto element_bytes =
                integer_elements ? constant->type->element->bits / 8 : 0;
            if (!constant->type || constant->type->kind != TypeKind::Array ||
                !integer_elements || element_bytes == 0 ||
                data.bytes.size() !=
                    constant->type->count * element_bytes) {
                return fail("data-array constant has an invalid type or length");
            }
        }
        if (constant->kind == ValueKind::ConstantAddressArray) {
            const auto& addresses =
                *static_cast<const ConstantAddressArray*>(constant.get());
            // The elements are addresses, so the element type has to hold one:
            // its width is the module's pointer width, not a fixed 32 bits (rule
            // 12 has to hold on every target the module names).
            const auto pointer_bits =
                target::get_target(module.target()).pointer_bits;
            const bool word_elements =
                constant->type && constant->type->element &&
                is_integer(constant->type->element) &&
                constant->type->element->bits == pointer_bits;
            const bool pointer_elements =
                constant->type && constant->type->element &&
                constant->type->element->kind == TypeKind::Pointer;
            if (!constant->type || constant->type->kind != TypeKind::Array ||
                (!word_elements && !pointer_elements) ||
                addresses.elements.size() != constant->type->count) {
                return fail("address-array constant has an invalid type or length");
            }
            for (const auto* element : addresses.elements) {
                if (!element) {
                    return fail("address-array constant has a null element");
                }
                const bool is_address = element->kind == ValueKind::Function ||
                                        element->kind == ValueKind::Global;
                const bool is_zero =
                    element->kind == ValueKind::ConstantInt &&
                    static_cast<const ConstantInt*>(element)->value == 0;
                if (!is_address && !is_zero) {
                    return fail("address-array element is not an address");
                }
            }
        }
    }

    for (const auto& global : module.globals()) {
        if (!global->element_type) {
            return fail("global '" + global->name + "' has no element type");
        }
        if (!is_pointer(global->type) ||
            !same_type(global->type->element, global->element_type)) {
            return fail("global '" + global->name + "' pointer type is inconsistent");
        }
        if (global->initializer && !same_type(global->initializer->type, global->element_type)) {
            return fail("global '" + global->name + "' initializer type mismatch");
        }
    }

    for (const auto& function_ptr : module.functions()) {
        const auto& function = *function_ptr;
        const auto function_fail = [&](std::string message) {
            return fail("function '" + function.name + "': " + std::move(message));
        };
        if (!function.type || function.type->kind != TypeKind::Function) {
            return function_fail("value does not have a function type");
        }
        if (function.declaration) {
            if (!function.blocks.empty()) {
                return function_fail("declaration has basic blocks");
            }
            if (function.parameters.size() != function.type->parameters.size()) {
                return function_fail("parameter count does not match its function type");
            }
            for (std::size_t index = 0; index < function.parameters.size(); ++index) {
                if (!same_type(function.parameters[index]->type,
                               function.type->parameters[index])) {
                    return function_fail("parameter type does not match the function type");
                }
            }
            continue;
        }
        if (function.blocks.empty()) {
            return function_fail("has no basic blocks");
        }
        if (function.parameters.size() != function.type->parameters.size()) {
            return function_fail("parameter count does not match its function type");
        }
        for (std::size_t index = 0; index < function.parameters.size(); ++index) {
            if (!same_type(function.parameters[index]->type,
                           function.type->parameters[index])) {
                return function_fail("parameter type does not match the function type");
            }
        }

        std::unordered_map<const BasicBlock*, std::size_t> block_index;
        for (std::size_t index = 0; index < function.blocks.size(); ++index) {
            const auto& block = function.blocks[index];
            if (block->parent != &function) {
                return function_fail("block has the wrong parent function");
            }
            if (block_index.find(block.get()) != block_index.end()) {
                return function_fail("duplicate basic block");
            }
            block_index.emplace(block.get(), index);
        }

        std::unordered_map<const Instruction*, std::pair<const BasicBlock*, std::size_t>> defs;
        std::unordered_set<const Parameter*> parameters;
        for (const auto& parameter : function.parameters) {
            parameters.insert(parameter.get());
        }

        for (const auto& block : function.blocks) {
            bool saw_non_phi = false;
            for (std::size_t index = 0; index < block->instructions.size(); ++index) {
                const auto& instruction = *block->instructions[index];
                if (instruction.parent != block.get()) {
                    return function_fail("instruction has the wrong parent block");
                }
                if (instruction.opcode == Opcode::Phi) {
                    if (saw_non_phi) {
                        return function_fail("phi node appears after a non-phi instruction");
                    }
                } else {
                    saw_non_phi = true;
                }
                if (is_terminator(instruction.opcode) &&
                    index + 1 != block->instructions.size()) {
                    return function_fail("instruction appears after a terminator");
                }
                if (!instruction.type) {
                    return function_fail("instruction has no result type");
                }
                defs.emplace(&instruction, std::make_pair(block.get(), index));
            }
            if (!block->terminated()) {
                return function_fail("block '" + block->name + "' has no terminator");
            }
        }

        std::unordered_map<const BasicBlock*, std::vector<const BasicBlock*>> predecessors;
        for (const auto& block : function.blocks) {
            predecessors[block.get()];
        }
        for (const auto& block : function.blocks) {
            const auto* terminator = block->terminator();
            if (!terminator) {
                continue;
            }
            std::vector<const BasicBlock*> targets;
            if (terminator->opcode == Opcode::Switch) {
                if (terminator->default_successor) {
                    targets.push_back(terminator->default_successor);
                }
                for (const auto& switch_case : terminator->switch_cases) {
                    if (switch_case.second) {
                        targets.push_back(switch_case.second);
                    }
                }
            } else {
                for (const auto* target : terminator->successors) {
                    targets.push_back(target);
                }
            }
            for (const auto* target : targets) {
                if (!target || block_index.find(target) == block_index.end()) {
                    return function_fail("branch target is not a block in this function");
                }
                predecessors[target].push_back(block.get());
            }
        }

        std::unordered_set<const BasicBlock*> reachable;
        std::vector<const BasicBlock*> worklist{function.entry_block()};
        while (!worklist.empty()) {
            const auto* block = worklist.back();
            worklist.pop_back();
            if (!block || !reachable.insert(block).second) {
                continue;
            }
            const auto* terminator = block->terminator();
            if (!terminator) {
                continue;
            }
            if (terminator->opcode == Opcode::Switch) {
                if (terminator->default_successor) {
                    worklist.push_back(terminator->default_successor);
                }
                for (const auto& switch_case : terminator->switch_cases) {
                    if (switch_case.second) {
                        worklist.push_back(switch_case.second);
                    }
                }
            } else {
                for (const auto* target : terminator->successors) {
                    if (target) {
                        worklist.push_back(target);
                    }
                }
            }
        }

        std::vector<std::unordered_set<const BasicBlock*>> dominators(function.blocks.size());
        for (std::size_t index = 0; index < function.blocks.size(); ++index) {
            if (index == 0) {
                dominators[index].insert(function.blocks[index].get());
            } else {
                for (const auto& candidate : function.blocks) {
                    dominators[index].insert(candidate.get());
                }
            }
        }
        bool changed = true;
        while (changed) {
            changed = false;
            for (std::size_t index = 1; index < function.blocks.size(); ++index) {
                const auto* block = function.blocks[index].get();
                std::unordered_set<const BasicBlock*> next;
                const auto& preds = predecessors[block];
                if (preds.empty()) {
                    next.insert(block);
                } else {
                    next = dominators[block_index.at(preds.front())];
                    for (std::size_t pred_index = 1; pred_index < preds.size(); ++pred_index) {
                        std::unordered_set<const BasicBlock*> intersection;
                        const auto& other = dominators[block_index.at(preds[pred_index])];
                        for (const auto* candidate : next) {
                            if (other.find(candidate) != other.end()) {
                                intersection.insert(candidate);
                            }
                        }
                        next = std::move(intersection);
                    }
                    next.insert(block);
                }
                if (next != dominators[index]) {
                    dominators[index] = std::move(next);
                    changed = true;
                }
            }
        }

        const auto dominates = [&](const BasicBlock* definition, const BasicBlock* use) {
            if (!definition || !use) {
                return false;
            }
            const auto& set = dominators[block_index.at(use)];
            return definition == use || set.find(definition) != set.end();
        };

        const auto check_value = [&](const Value* value, const BasicBlock* use_block,
                                     std::size_t use_index, bool is_phi) {
            if (!value) {
                return function_fail("instruction has a null operand");
            }
            if (value->kind == ValueKind::Parameter) {
                if (parameters.find(static_cast<const Parameter*>(value)) == parameters.end()) {
                    return function_fail("operand is a parameter of another function");
                }
                return true;
            }
            if (value->kind == ValueKind::Instruction) {
                const auto found = defs.find(static_cast<const Instruction*>(value));
                if (found == defs.end()) {
                    return function_fail("operand is defined outside this function");
                }
                const auto* definition_block = found->second.first;
                const auto definition_index = found->second.second;
                if (definition_block == use_block && !is_phi &&
                    definition_index >= use_index) {
                    return function_fail("instruction does not dominate its use");
                }
                if (definition_block != use_block) {
                    if (reachable.find(use_block) != reachable.end() &&
                        !dominates(definition_block, use_block)) {
                        return function_fail("instruction does not dominate its use");
                    }
                }
                return true;
            }
            return true;
        };

        const auto check_successors = [&](const Instruction& instruction,
                                          std::size_t expected) {
            if (instruction.successors.size() != expected) {
                return function_fail("terminator has the wrong successor count");
            }
            for (const auto* successor : instruction.successors) {
                if (!successor || block_index.find(successor) == block_index.end()) {
                    return function_fail("terminator target is not in this function");
                }
            }
            return true;
        };

        for (const auto& block : function.blocks) {
            std::size_t instruction_index = 0;
            for (const auto& instruction_ptr : block->instructions) {
                const auto& instruction = *instruction_ptr;
                const auto& operands = instruction.operands;
                const auto require_operands = [&](std::size_t count) {
                    if (operands.size() != count) {
                        return function_fail("instruction '" +
                                             std::string(opcode_name(instruction.opcode)) +
                                             "' has the wrong operand count");
                    }
                    return true;
                };
                const auto check_operand_types = [&]() {
                    for (const auto* value : operands) {
                        if (!check_value(value, block.get(), instruction_index, false)) {
                            return false;
                        }
                    }
                    return true;
                };

                switch (instruction.opcode) {
                case Opcode::Add:
                case Opcode::Sub:
                case Opcode::Mul:
                case Opcode::UDiv:
                case Opcode::SDiv:
                case Opcode::URem:
                case Opcode::SRem:
                case Opcode::Shl:
                case Opcode::LShr:
                case Opcode::AShr:
                case Opcode::And:
                case Opcode::Or:
                case Opcode::Xor:
                    if (!require_operands(2) || !check_operand_types()) {
                        return false;
                    }
                    if (!same_type(operands[0]->type, operands[1]->type) ||
                        !same_type(instruction.type, operands[0]->type) ||
                        !is_integer(instruction.type)) {
                        return function_fail(
                            "integer binary operation has mismatched types: " +
                            std::string(opcode_name(instruction.opcode)) + " result " +
                            type_to_string(instruction.type) + " operands " +
                            type_to_string(operands[0]->type) + ", " +
                            type_to_string(operands[1]->type));
                    }
                    break;
                case Opcode::FAdd:
                case Opcode::FSub:
                case Opcode::FMul:
                case Opcode::FDiv:
                case Opcode::FRem:
                    if (!require_operands(2) || !check_operand_types()) {
                        return false;
                    }
                    if (!same_type(operands[0]->type, operands[1]->type) ||
                        !same_type(instruction.type, operands[0]->type) ||
                        !is_float(instruction.type)) {
                        return function_fail("floating binary operation has mismatched types");
                    }
                    break;
                case Opcode::Trunc:
                case Opcode::ZExt:
                case Opcode::SExt:
                    if (!require_operands(1) || !check_operand_types()) {
                        return false;
                    }
                    if (!is_integer(operands[0]->type) || !is_integer(instruction.type)) {
                        return function_fail("integer cast has a non-integer type");
                    }
                    break;
                case Opcode::FPTrunc:
                case Opcode::FPExt:
                    if (!require_operands(1) || !check_operand_types()) {
                        return false;
                    }
                    if (!is_float(operands[0]->type) || !is_float(instruction.type)) {
                        return function_fail("floating cast has a non-floating type");
                    }
                    break;
                case Opcode::FPToUI:
                case Opcode::FPToSI:
                    if (!require_operands(1) || !check_operand_types()) {
                        return false;
                    }
                    if (!is_float(operands[0]->type) || !is_integer(instruction.type)) {
                        return function_fail("float-to-integer cast has invalid types");
                    }
                    break;
                case Opcode::UIToFP:
                case Opcode::SIToFP:
                    if (!require_operands(1) || !check_operand_types()) {
                        return false;
                    }
                    if (!is_integer(operands[0]->type) || !is_float(instruction.type)) {
                        return function_fail("integer-to-float cast has invalid types");
                    }
                    break;
                case Opcode::BitCast:
                    if (!require_operands(1) || !check_operand_types()) {
                        return false;
                    }
                    if (is_void(instruction.type) || is_void(operands[0]->type)) {
                        return function_fail("bitcast cannot use void");
                    }
                    break;
                case Opcode::PtrToInt:
                    if (!require_operands(1) || !check_operand_types()) {
                        return false;
                    }
                    if (!is_pointer(operands[0]->type) || !is_integer(instruction.type)) {
                        return function_fail("ptrtoint has invalid types");
                    }
                    break;
                case Opcode::IntToPtr:
                    if (!require_operands(1) || !check_operand_types()) {
                        return false;
                    }
                    if (!is_integer(operands[0]->type) || !is_pointer(instruction.type)) {
                        return function_fail("inttoptr has invalid types");
                    }
                    break;
                case Opcode::ICmp:
                    if (!require_operands(2) || !check_operand_types()) {
                        return false;
                    }
                    if (!same_type(operands[0]->type, operands[1]->type) ||
                        !(is_integer(operands[0]->type) || is_pointer(operands[0]->type)) ||
                        !is_i1_type(instruction.type)) {
                        return function_fail("icmp has invalid operand or result types");
                    }
                    break;
                case Opcode::FCmp:
                    if (!require_operands(2) || !check_operand_types()) {
                        return false;
                    }
                    if (!same_type(operands[0]->type, operands[1]->type) ||
                        !is_float(operands[0]->type) ||
                        !is_i1_type(instruction.type)) {
                        return function_fail("fcmp has invalid operand or result types");
                    }
                    break;
                case Opcode::Load:
                    if (!require_operands(1) || !check_operand_types()) {
                        return false;
                    }
                    if (!is_pointer(operands[0]->type) || !operands[0]->type->element ||
                        !same_type(instruction.type, operands[0]->type->element)) {
                        return function_fail("load type does not match its pointer");
                    }
                    break;
                case Opcode::Store:
                    if (!require_operands(2) || !check_operand_types()) {
                        return false;
                    }
                    if (!is_pointer(operands[1]->type) || !operands[1]->type->element ||
                        !same_type(operands[0]->type, operands[1]->type->element) ||
                        !is_void(instruction.type)) {
                        return function_fail("store type does not match its pointer");
                    }
                    break;
                case Opcode::Alloca:
                    if (!require_operands(0) || !instruction.allocated_type ||
                        !is_pointer(instruction.type) ||
                        !same_type(instruction.type->element, instruction.allocated_type)) {
                        return function_fail("alloca has an invalid allocated type");
                    }
                    break;
                case Opcode::VaStart:
                    if (!require_operands(0) || !is_pointer(instruction.type) ||
                        !instruction.type->element ||
                        instruction.type->element->kind != TypeKind::Integer ||
                        instruction.type->element->bits != 8) {
                        return function_fail("vastart must produce a ptr<i8>");
                    }
                    break;
                case Opcode::GetElementPtr:
                    if (operands.empty() || !check_operand_types()) {
                        return false;
                    }
                    if (!is_pointer(operands[0]->type) || !instruction.source_type ||
                        !is_pointer(instruction.type)) {
                        return function_fail("getelementptr has an invalid base or result");
                    }
                    for (std::size_t index = 1; index < operands.size(); ++index) {
                        if (!is_integer(operands[index]->type)) {
                            return function_fail("getelementptr index is not an integer");
                        }
                    }
                    break;
                case Opcode::Memcpy:
                    if (!require_operands(3) || !check_operand_types()) {
                        return false;
                    }
                    if (!is_pointer(operands[0]->type) || !is_pointer(operands[1]->type) ||
                        !is_integer(operands[2]->type) || !is_void(instruction.type)) {
                        return function_fail("memcpy has invalid operand types");
                    }
                    break;
                case Opcode::Memset:
                    if (!require_operands(3) || !check_operand_types()) {
                        return false;
                    }
                    if (!is_pointer(operands[0]->type) || !is_integer(operands[1]->type) ||
                        !is_integer(operands[2]->type) || !is_void(instruction.type)) {
                        return function_fail("memset has invalid operand types");
                    }
                    break;
                case Opcode::Call:
                case Opcode::Invoke: {
                    if (operands.empty() || !check_operand_types()) {
                        return false;
                    }
                    TypePtr callee_type = operands[0]->type;
                    if (callee_type && callee_type->kind == TypeKind::Pointer) {
                        callee_type = callee_type->element;
                    }
                    if (!callee_type || callee_type->kind != TypeKind::Function) {
                        return function_fail("call target is not a function");
                    }
                    if (operands.size() - 1 < callee_type->parameters.size()) {
                        return function_fail("call has too few arguments");
                    }
                    if (!callee_type->variadic && !callee_type->unprototyped &&
                        operands.size() - 1 != callee_type->parameters.size()) {
                        return function_fail("call has the wrong argument count");
                    }
                    for (std::size_t index = 0; index < callee_type->parameters.size();
                         ++index) {
                        if (!same_type(operands[index + 1]->type,
                                       callee_type->parameters[index])) {
                            return function_fail("call argument type mismatch");
                        }
                    }
                    if (!same_type(instruction.type, callee_type->return_type)) {
                        return function_fail("call result type mismatch");
                    }
                    // An invoke has a normal edge and the landing pad the
                    // unwinder enters instead of returning.
                    if (instruction.opcode == Opcode::Invoke &&
                        !check_successors(instruction, 2)) {
                        return false;
                    }
                    break;
                }
                case Opcode::LandingPad:
                    // Entered by the unwinder through an invoke's unwind edge,
                    // so it must open its block and that block takes no
                    // ordinary branch.
                    if (!require_operands(0) || !is_pointer(instruction.type) ||
                        !instruction.type->element ||
                        instruction.type->element->kind != TypeKind::Integer ||
                        instruction.type->element->bits != 8) {
                        return function_fail("landingpad must produce a ptr<i8>");
                    }
                    if (instruction_index != 0) {
                        return function_fail("landingpad must open its block");
                    }
                    break;
                case Opcode::Resume:
                    if (!require_operands(1) || !check_operand_types()) {
                        return false;
                    }
                    if (!is_pointer(operands[0]->type) ||
                        !is_void(instruction.type)) {
                        return function_fail("resume takes a pointer and produces void");
                    }
                    break;
                case Opcode::Intrinsic: {
                    if (!check_operand_types()) {
                        return false;
                    }
                    const auto is_matrix = [](TypePtr type) {
                        return is_float_lane_record(type, 16);
                    };
                    const auto is_vector = [](TypePtr type) {
                        return is_float_lane_record(type, 4);
                    };
                    std::size_t expected = 0;
                    switch (instruction.intrinsic) {
                    case Intrinsic::MatrixMultiply:
                    case Intrinsic::MatrixMultiplyAdd:
                        expected = instruction.intrinsic == Intrinsic::MatrixMultiply ? 2 : 3;
                        if (!is_matrix(instruction.type)) {
                            return function_fail("matrix multiply must produce a 4x4 tile");
                        }
                        break;
                    case Intrinsic::MatrixTranspose:
                    case Intrinsic::TransformPush:
                    case Intrinsic::TransformPop:
                        expected = 1;
                        if (instruction.intrinsic == Intrinsic::MatrixTranspose &&
                            !is_matrix(instruction.type)) {
                            return function_fail("matrix transpose must produce a 4x4 tile");
                        }
                        if (instruction.intrinsic != Intrinsic::MatrixTranspose &&
                            !is_void(instruction.type)) {
                            return function_fail("transform stack operation must be void");
                        }
                        break;
                    case Intrinsic::MatrixVectorTransform:
                        expected = 2;
                        if (!is_vector(instruction.type)) {
                            return function_fail("matrix transform must produce a 4-lane vector");
                        }
                        break;
                    case Intrinsic::VectorDot:
                        expected = 2;
                        if (!is_float(instruction.type) || instruction.type->bits != 32) {
                            return function_fail("vector dot must produce f32");
                        }
                        break;
                    case Intrinsic::TransformDrop:
                        expected = 0;
                        if (!is_void(instruction.type)) {
                            return function_fail("transform drop must be void");
                        }
                        break;
                    case Intrinsic::None:
                        return function_fail("intrinsic is not named");
                    }
                    if (operands.size() != expected) {
                        return function_fail("intrinsic '" +
                                             std::string(intrinsic_name(instruction.intrinsic)) +
                                             "' has the wrong operand count");
                    }
                    for (const auto* value : operands) {
                        if (!value || !is_pointer(value->type) || !value->type->element ||
                            value->type->element->kind != TypeKind::Struct) {
                            return function_fail(
                                "intrinsic '" +
                                std::string(intrinsic_name(instruction.intrinsic)) +
                                "' expects record pointers");
                        }
                    }
                    break;
                }
                case Opcode::Br:
                    if (!check_successors(instruction, 1)) {
                        return false;
                    }
                    break;
                case Opcode::CondBr:
                    if (!require_operands(1) || !check_operand_types() ||
                        !check_successors(instruction, 2)) {
                        return false;
                    }
                    if (!is_i1_type(operands[0]->type)) {
                        return function_fail("condbr condition is not i1");
                    }
                    break;
                case Opcode::Switch:
                    if (!require_operands(1) || !check_operand_types()) {
                        return false;
                    }
                    if (!is_integer(operands[0]->type) || !instruction.default_successor) {
                        return function_fail("switch has an invalid condition or default");
                    }
                    for (const auto& switch_case : instruction.switch_cases) {
                        if (!switch_case.first || !switch_case.second ||
                            !same_type(switch_case.first->type, operands[0]->type) ||
                            block_index.find(switch_case.second) == block_index.end()) {
                            return function_fail("switch case is invalid");
                        }
                    }
                    break;
                case Opcode::Ret:
                    if (is_void(function.type->return_type)) {
                        if (!operands.empty()) {
                            return function_fail("void function returns a value");
                        }
                    } else {
                        if (operands.size() != 1 ||
                            !same_type(operands[0]->type, function.type->return_type)) {
                            return function_fail("return type does not match the function");
                        }
                    }
                    break;
                case Opcode::Unreachable:
                    if (!require_operands(0)) {
                        return false;
                    }
                    break;
                case Opcode::Phi: {
                    const auto& preds = predecessors[block.get()];
                    if (instruction.incoming.size() != preds.size()) {
                        return function_fail("phi incoming count does not match predecessors");
                    }
                    std::unordered_set<const BasicBlock*> seen_incoming;
                    for (const auto& incoming : instruction.incoming) {
                        if (!incoming.first || !incoming.second ||
                            block_index.find(incoming.second) == block_index.end()) {
                            return function_fail("phi has an invalid incoming edge");
                        }
                        if (!seen_incoming.insert(incoming.second).second) {
                            return function_fail("phi has duplicate incoming blocks");
                        }
                        if (!same_type(incoming.first->type, instruction.type)) {
                            return function_fail("phi incoming type mismatch");
                        }
                        if (!check_value(incoming.first, incoming.second, 0, true)) {
                            return false;
                        }
                    }
                    break;
                }
                case Opcode::Select:
                    if (!require_operands(3) || !check_operand_types()) {
                        return false;
                    }
                    if (!is_i1_type(operands[0]->type) ||
                        !same_type(operands[1]->type, operands[2]->type) ||
                        !same_type(instruction.type, operands[1]->type)) {
                        return function_fail("select has mismatched types");
                    }
                    break;
                case Opcode::ExtractValue:
                case Opcode::InsertValue: {
                    const std::size_t expected = instruction.opcode == Opcode::ExtractValue
                                                     ? 1
                                                     : 2;
                    if (!require_operands(expected) || !check_operand_types()) {
                        return false;
                    }
                    TypePtr current = operands[0]->type;
                    for (const auto index : instruction.aggregate_indices) {
                        if (!current) {
                            return function_fail("aggregate index on a null type");
                        }
                        if (current->kind == TypeKind::Struct ||
                            current->kind == TypeKind::Union) {
                            if (index >= current->fields.size()) {
                                return function_fail("aggregate index out of range");
                            }
                            current = current->fields[index];
                        } else if (current->kind == TypeKind::Array) {
                            current = current->element;
                        } else {
                            return function_fail("aggregate index on a non-aggregate");
                        }
                    }
                    if (instruction.opcode == Opcode::ExtractValue) {
                        if (!same_type(instruction.type, current)) {
                            return function_fail("extractvalue result type mismatch");
                        }
                    } else if (!same_type(operands[1]->type, current) ||
                               !same_type(instruction.type, operands[0]->type)) {
                        return function_fail("insertvalue type mismatch");
                    }
                    break;
                }
                }
                ++instruction_index;
            }
        }
    }

    return true;
}

}  // namespace krc::ir

