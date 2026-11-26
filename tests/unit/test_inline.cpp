#include "doctest.h"

#include <memory>
#include <string>

#include "ir/ir.h"
#include "ir/passes.h"
#include "krc/target/target.h"

using namespace krc::ir;

namespace {

struct Fixture {
    Module module{krc::target::TargetId::Kr32};
    Function* callee = nullptr;
    Function* caller = nullptr;
    Instruction* call = nullptr;

    Fixture() {
        const auto i32 = module.types().int_type(32);
        const auto callee_type = module.types().function_type(i32, {i32});
        callee = module.add_function("inc", callee_type, Linkage::Internal);
        auto* parameter = callee->add_parameter(i32, "value");
        auto* callee_entry = callee->add_block("entry");
        IRBuilder builder(module.types());
        builder.set_insert_point(callee_entry);
        auto* sum = builder.create_add(parameter, module.constant_int(i32, 1), "sum");
        builder.create_ret(sum);

        const auto caller_type = module.types().function_type(i32, {});
        caller = module.add_function("caller", caller_type, Linkage::External);
        auto* caller_entry = caller->add_block("entry");
        builder.set_insert_point(caller_entry);
        call = builder.create_call(callee, {module.constant_int(i32, 41)}, i32, "result");
        builder.create_ret(call);
    }
};

}  // namespace

TEST_CASE("A single-call leaf function is inlined") {
    Fixture fixture;
    const auto change = inline_small_functions(fixture.module);
    CHECK(change.functions_inlined == 1);
    REQUIRE(fixture.caller->blocks.size() == 1);
    const auto& instructions = fixture.caller->blocks.front()->instructions;
    REQUIRE(instructions.size() == 2);
    CHECK(instructions.front()->opcode == Opcode::Add);
    CHECK(instructions.back()->opcode == Opcode::Ret);
    CHECK(instructions.back()->operands.front() == instructions.front().get());
    std::string error;
    CHECK(Verifier().verify(fixture.module, error));
}

TEST_CASE("A second call site prevents inlining") {
    Fixture fixture;
    const auto i32 = fixture.module.types().int_type(32);
    IRBuilder builder(fixture.module.types());
    auto* entry = fixture.caller->blocks.front().get();
    entry->instructions.pop_back();
    builder.set_insert_point(entry);
    auto* second = builder.create_call(fixture.callee,
                                       {fixture.module.constant_int(i32, 1)}, i32,
                                       "second");
    builder.create_ret(second);
    const auto change = inline_small_functions(fixture.module);
    CHECK(change.functions_inlined == 0);
}
