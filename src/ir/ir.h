#pragma once

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "krc/target/target.h"

namespace krc::ir {

class Module;
struct Function;
struct BasicBlock;
struct Instruction;

enum class TypeKind {
    Void,
    Integer,
    Float,
    Pointer,
    Array,
    Struct,
    Union,
    Function,
};

struct Type {
    TypeKind kind = TypeKind::Void;
    unsigned bits = 0;
    bool is_signed = false;
    const Type* element = nullptr;
    std::uint64_t count = 0;
    std::vector<const Type*> fields;
    std::vector<std::string> field_names;
    const Type* return_type = nullptr;
    std::vector<const Type*> parameters;
    bool variadic = false;
    bool packed = false;
    std::string name;
};

using TypePtr = const Type*;

class TypeContext {
public:
    TypeContext() = default;
    TypeContext(const TypeContext&) = delete;
    TypeContext& operator=(const TypeContext&) = delete;

    [[nodiscard]] TypePtr void_type();
    [[nodiscard]] TypePtr bool_type();
    [[nodiscard]] TypePtr int_type(unsigned bits, bool is_signed = true);
    [[nodiscard]] TypePtr float_type(unsigned bits);
    [[nodiscard]] TypePtr pointer_type(TypePtr element);
    [[nodiscard]] TypePtr array_type(TypePtr element, std::uint64_t count);
    [[nodiscard]] TypePtr struct_type(std::string name, std::vector<TypePtr> fields,
                                      std::vector<std::string> field_names = {},
                                      bool packed = false);
    [[nodiscard]] TypePtr struct_forward(std::string name);
    [[nodiscard]] bool complete_struct(TypePtr type, std::vector<TypePtr> fields,
                                       std::vector<std::string> field_names = {},
                                       bool packed = false);
    [[nodiscard]] TypePtr union_type(std::string name, std::vector<TypePtr> fields,
                                     std::vector<std::string> field_names = {});
    [[nodiscard]] TypePtr function_type(TypePtr return_type, std::vector<TypePtr> parameters,
                                        bool variadic = false);

    [[nodiscard]] const std::vector<TypePtr>& named_types() const { return named_types_; }

private:
    TypePtr make(Type type);
    TypePtr primitive(TypeKind kind, unsigned bits, bool is_signed);
    std::vector<std::unique_ptr<Type>> types_;
    std::vector<TypePtr> named_types_;
    TypePtr void_ = nullptr;
    TypePtr bool_ = nullptr;
    TypePtr i8_ = nullptr;
    TypePtr i16_ = nullptr;
    TypePtr i32_ = nullptr;
    TypePtr i64_ = nullptr;
    TypePtr f32_ = nullptr;
    TypePtr f64_ = nullptr;
};

[[nodiscard]] bool same_type(TypePtr left, TypePtr right);
[[nodiscard]] bool is_integer(TypePtr type);
[[nodiscard]] bool is_float(TypePtr type);
[[nodiscard]] bool is_pointer(TypePtr type);
[[nodiscard]] bool is_void(TypePtr type);
[[nodiscard]] std::string type_to_string(TypePtr type);

enum class Linkage {
    External,
    Internal,
    Weak,
};

enum class CallingConvention {
    C,
};

enum class ValueKind {
    Function,
    Global,
    Parameter,
    Instruction,
    ConstantInt,
    ConstantFloat,
    ConstantNull,
    ConstantUndef,
    ConstantDataArray,
};

struct Value {
    ValueKind kind = ValueKind::Instruction;
    TypePtr type = nullptr;
    std::string name;
    Value() = default;
    Value(ValueKind value_kind, TypePtr value_type, std::string value_name)
        : kind(value_kind), type(value_type), name(std::move(value_name)) {}
    virtual ~Value() = default;
};

struct Constant : Value {};

struct ConstantInt final : Constant {
    std::int64_t value = 0;
};

struct ConstantFloat final : Constant {
    double value = 0.0;
};

struct ConstantNull final : Constant {};

struct ConstantUndef final : Constant {};

struct ConstantDataArray final : Constant {
    std::vector<std::uint8_t> bytes;
};

struct DebugLocation {
    std::string file;
    std::uint32_t line = 0;
    std::uint32_t column = 0;

    [[nodiscard]] bool valid() const { return !file.empty() && line != 0; }
};

enum class Opcode {
    Add,
    Sub,
    Mul,
    UDiv,
    SDiv,
    URem,
    SRem,
    FAdd,
    FSub,
    FMul,
    FDiv,
    FRem,
    Shl,
    LShr,
    AShr,
    And,
    Or,
    Xor,
    Trunc,
    ZExt,
    SExt,
    FPTrunc,
    FPExt,
    FPToUI,
    FPToSI,
    UIToFP,
    SIToFP,
    BitCast,
    PtrToInt,
    IntToPtr,
    ICmp,
    FCmp,
    Load,
    Store,
    Alloca,
    VaStart,
    GetElementPtr,
    Memcpy,
    Memset,
    Call,
    Br,
    CondBr,
    Switch,
    Ret,
    Unreachable,
    Phi,
    Select,
    ExtractValue,
    InsertValue,
    Invoke,
    LandingPad,
    Resume,
};

enum class ICmpPredicate {
    Eq,
    Ne,
    Ugt,
    Uge,
    Ult,
    Ule,
    Sgt,
    Sge,
    Slt,
    Sle,
};

enum class FCmpPredicate {
    False,
    Oeq,
    Ogt,
    Oge,
    Olt,
    Ole,
    One,
    Ord,
    Uno,
    True,
};

[[nodiscard]] bool is_terminator(Opcode opcode);
[[nodiscard]] bool is_reserved_v11(Opcode opcode);
[[nodiscard]] std::string_view opcode_name(Opcode opcode);
[[nodiscard]] std::string_view icmp_predicate_name(ICmpPredicate predicate);
[[nodiscard]] std::string_view fcmp_predicate_name(FCmpPredicate predicate);

struct Parameter final : Value {
    Parameter(TypePtr type, std::string name);
};

struct Instruction final : Value {
    Opcode opcode = Opcode::Unreachable;
    BasicBlock* parent = nullptr;
    std::vector<Value*> operands;
    std::vector<BasicBlock*> successors;
    std::vector<std::pair<Value*, BasicBlock*>> incoming;
    std::vector<std::pair<ConstantInt*, BasicBlock*>> switch_cases;
    BasicBlock* default_successor = nullptr;
    ICmpPredicate icmp_predicate = ICmpPredicate::Eq;
    FCmpPredicate fcmp_predicate = FCmpPredicate::False;
    std::uint32_t alignment = 0;
    bool is_volatile = false;
    bool inbounds = false;
    TypePtr allocated_type = nullptr;
    TypePtr source_type = nullptr;
    std::vector<unsigned> aggregate_indices;
    DebugLocation debug;
};

struct BasicBlock {
    std::string name;
    Function* parent = nullptr;
    std::vector<std::unique_ptr<Instruction>> instructions;

    [[nodiscard]] Instruction* terminator() const;
    [[nodiscard]] bool terminated() const { return terminator() != nullptr; }
};

struct Function final : Value {
    Linkage linkage = Linkage::External;
    CallingConvention calling_convention = CallingConvention::C;
    bool declaration = false;
    std::vector<std::unique_ptr<Parameter>> parameters;
    std::vector<std::unique_ptr<BasicBlock>> blocks;

    Function(TypePtr type, std::string name);

    Parameter* add_parameter(TypePtr parameter_type, std::string parameter_name);
    BasicBlock* add_block(std::string block_name);
    [[nodiscard]] BasicBlock* entry_block() const;
    [[nodiscard]] BasicBlock* find_block(std::string_view block_name) const;
};

struct GlobalVariable final : Value {
    TypePtr element_type = nullptr;
    Value* initializer = nullptr;
    bool constant = false;
    std::uint32_t alignment = 0;
};

class Module {
public:
    explicit Module(target::TargetId target = target::TargetId::Kr32);

    [[nodiscard]] TypeContext& types() { return types_; }
    [[nodiscard]] const TypeContext& types() const { return types_; }
    [[nodiscard]] target::TargetId target() const { return target_; }

    Function* add_function(std::string name, TypePtr function_type, Linkage linkage,
                           bool declaration = false);
    GlobalVariable* add_global(std::string name, TypePtr element_type, bool constant = false,
                               std::uint32_t alignment = 0);
    ConstantInt* constant_int(TypePtr type, std::int64_t value);
    ConstantFloat* constant_float(TypePtr type, double value);
    ConstantNull* constant_null(TypePtr type);
    ConstantUndef* constant_undef(TypePtr type);
    ConstantDataArray* constant_data_array(TypePtr array_type,
                                           std::vector<std::uint8_t> bytes);

    [[nodiscard]] const std::vector<std::unique_ptr<Function>>& functions() const {
        return functions_;
    }
    [[nodiscard]] const std::vector<std::unique_ptr<GlobalVariable>>& globals() const {
        return globals_;
    }
    [[nodiscard]] const std::vector<std::unique_ptr<Constant>>& constants() const {
        return constants_;
    }

private:
    target::TargetId target_;
    TypeContext types_;
    std::vector<std::unique_ptr<Function>> functions_;
    std::vector<std::unique_ptr<GlobalVariable>> globals_;
    std::vector<std::unique_ptr<Constant>> constants_;
};

class IRBuilder {
public:
    explicit IRBuilder(TypeContext& types);

    void set_insert_point(BasicBlock* block);
    void set_debug_location(DebugLocation location);
    void reset_value_names();
    [[nodiscard]] BasicBlock* insert_block() const { return block_; }

    Instruction* create(Opcode opcode, TypePtr type, std::vector<Value*> operands,
                        std::string name = {});
    Instruction* create_add(Value* left, Value* right, std::string name = {});
    Instruction* create_sub(Value* left, Value* right, std::string name = {});
    Instruction* create_mul(Value* left, Value* right, std::string name = {});
    Instruction* create_sdiv(Value* left, Value* right, std::string name = {});
    Instruction* create_udiv(Value* left, Value* right, std::string name = {});
    Instruction* create_srem(Value* left, Value* right, std::string name = {});
    Instruction* create_and(Value* left, Value* right, std::string name = {});
    Instruction* create_or(Value* left, Value* right, std::string name = {});
    Instruction* create_xor(Value* left, Value* right, std::string name = {});
    Instruction* create_shl(Value* left, Value* right, std::string name = {});
    Instruction* create_ashr(Value* left, Value* right, std::string name = {});
    Instruction* create_lshr(Value* left, Value* right, std::string name = {});
    Instruction* create_icmp(ICmpPredicate predicate, Value* left, Value* right,
                             std::string name = {});
    Instruction* create_fcmp(FCmpPredicate predicate, Value* left, Value* right,
                             std::string name = {});
    Instruction* create_load(TypePtr result_type, Value* pointer, bool is_volatile = false,
                             std::uint32_t alignment = 0, std::string name = {});
    Instruction* create_store(Value* value, Value* pointer, bool is_volatile = false,
                              std::uint32_t alignment = 0);
    Instruction* create_alloca(TypePtr allocated_type, std::uint32_t alignment = 0,
                               std::string name = {});
    Instruction* create_va_start(std::string name = {});
    Instruction* create_gep(TypePtr source_type, Value* base, std::vector<Value*> indices,
                            bool inbounds = false, std::string name = {});
    Instruction* create_call(Value* callee, std::vector<Value*> arguments,
                             TypePtr result_type = nullptr, std::string name = {});
    Instruction* create_br(BasicBlock* destination);
    Instruction* create_cond_br(Value* condition, BasicBlock* when_true, BasicBlock* when_false);
    Instruction* create_switch(Value* condition, BasicBlock* default_destination);
    Instruction* create_ret(Value* value = nullptr);
    Instruction* create_unreachable();
    Instruction* create_phi(TypePtr type, std::string name = {});
    Instruction* create_select(Value* condition, Value* when_true, Value* when_false,
                               std::string name = {});
    Instruction* create_extract_value(Value* aggregate, std::vector<unsigned> indices,
                                      TypePtr result_type, std::string name = {});
    Instruction* create_insert_value(Value* aggregate, Value* inserted,
                                     std::vector<unsigned> indices, std::string name = {});

private:
    Instruction* append(Opcode opcode, TypePtr type, std::vector<Value*> operands,
                        std::string name);
    [[nodiscard]] std::string make_name(std::string name);
    TypeContext& types_;
    BasicBlock* block_ = nullptr;
    std::uint64_t temp_counter_ = 0;
    std::unordered_set<std::string> value_names_;
    DebugLocation current_debug_;
};

class Verifier {
public:
    [[nodiscard]] bool verify(const Module& module, std::string& error) const;
};

void print_module(const Module& module, std::ostream& out);
void print_function(const Function& function, std::ostream& out);
void print_type(TypePtr type, std::ostream& out);

}  // namespace krc::ir
