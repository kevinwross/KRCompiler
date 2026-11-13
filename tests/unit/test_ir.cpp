#include <sstream>
#include <string>

#include "doctest.h"
#include "ir/ir.h"
#include "krc/target/target.h"

using namespace krc::ir;

namespace {

struct FunctionFixture {
    Module module;
    Function* function = nullptr;
    Parameter* left = nullptr;
    Parameter* right = nullptr;
    BasicBlock* entry = nullptr;

    explicit FunctionFixture(bool with_parameters = true)
        : module(krc::target::TargetId::Kr32) {
        const auto i32 = module.types().int_type(32);
        const auto type = module.types().function_type(
            i32, with_parameters ? std::vector<TypePtr>{i32, i32} : std::vector<TypePtr>{});
        function = module.add_function("test", type, Linkage::External);
        if (with_parameters) {
            left = function->add_parameter(i32, "left");
            right = function->add_parameter(i32, "right");
        }
        entry = function->add_block("entry");
    }
};

}  // namespace

TEST_CASE("IR builder constructs a verifiable function") {
    FunctionFixture fixture;
    IRBuilder builder(fixture.module.types());
    builder.set_insert_point(fixture.entry);
    auto* sum = builder.create_add(fixture.left, fixture.right, "sum");
    builder.create_ret(sum);

    Verifier verifier;
    std::string error;
    CHECK(verifier.verify(fixture.module, error));
    CHECK(error.empty());

    std::ostringstream out;
    print_module(fixture.module, out);
    const auto text = out.str();
    CHECK(text.find("; target: kr32") != std::string::npos);
    CHECK(text.find("define i32 @test(i32 %left, i32 %right)") != std::string::npos);
    CHECK(text.find("%sum = add i32 %left, %right") != std::string::npos);
    CHECK(text.find("ret i32 %sum") != std::string::npos);
}

TEST_CASE("IR verifier rejects mismatched binary types") {
    FunctionFixture fixture;
    IRBuilder builder(fixture.module.types());
    builder.set_insert_point(fixture.entry);
    const auto i64 = fixture.module.types().int_type(64);
    auto* bad = builder.create_add(fixture.left, fixture.module.constant_int(i64, 1), "bad");
    builder.create_ret(bad);

    Verifier verifier;
    std::string error;
    CHECK_FALSE(verifier.verify(fixture.module, error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("IR verifier rejects a missing terminator") {
    FunctionFixture fixture;
    IRBuilder builder(fixture.module.types());
    builder.set_insert_point(fixture.entry);
    builder.create_add(fixture.left, fixture.right, "sum");

    Verifier verifier;
    std::string error;
    CHECK_FALSE(verifier.verify(fixture.module, error));
    CHECK(error.find("terminator") != std::string::npos);
}

TEST_CASE("IR verifier checks phi predecessors and dominance") {
    Module module(krc::target::TargetId::Kr32);
    const auto i32 = module.types().int_type(32);
    const auto function_type = module.types().function_type(i32, {i32});
    auto* function = module.add_function("loop", function_type, Linkage::External);
    auto* parameter = function->add_parameter(i32, "start");
    auto* entry = function->add_block("entry");
    auto* loop = function->add_block("loop");
    auto* exit = function->add_block("exit");

    IRBuilder builder(module.types());
    builder.set_insert_point(entry);
    builder.create_br(loop);

    builder.set_insert_point(loop);
    auto* phi = builder.create_phi(i32, "value");
    auto* next = builder.create_add(phi, module.constant_int(i32, 1), "next");
    auto* condition = builder.create_icmp(ICmpPredicate::Slt, phi,
                                          module.constant_int(i32, 10), "condition");
    phi->incoming.push_back({parameter, entry});
    phi->incoming.push_back({next, loop});
    builder.create_cond_br(condition, loop, exit);

    builder.set_insert_point(exit);
    builder.create_ret(phi);

    Verifier verifier;
    std::string error;
    CHECK(verifier.verify(module, error));
    CHECK(error.empty());
}

TEST_CASE("IR verifies an invoke with a landing pad and resume") {
    Module module(krc::target::TargetId::Kr32);
    TypeContext& types = module.types();
    const auto i32 = types.int_type(32);
    const auto void_type = types.void_type();
    auto* callee = module.add_function("callee", types.function_type(void_type, {}),
                                       Linkage::External, true);
    auto* caller = module.add_function("caller", types.function_type(i32, {}),
                                      Linkage::External);
    auto* entry = caller->add_block("entry");
    auto* normal = caller->add_block("normal");
    auto* pad = caller->add_block("pad");

    IRBuilder builder(types);
    builder.set_insert_point(entry);
    builder.create_invoke(callee, {}, void_type, normal, pad);
    builder.set_insert_point(normal);
    builder.create_ret(module.constant_int(i32, 7));
    builder.set_insert_point(pad);
    auto* landing = builder.create_landingpad("exception");
    builder.create_resume(landing);

    Verifier verifier;
    std::string error;
    CHECK(verifier.verify(module, error));
    CHECK(error.empty());

    std::ostringstream out;
    print_module(module, out);
    CHECK(out.str().find("invoke void @callee() to label %normal unwind label %pad") !=
          std::string::npos);
    CHECK(out.str().find("landingpad ptr<i8>") != std::string::npos);
    CHECK(out.str().find("resume %exception") != std::string::npos);
}

TEST_CASE("IR printer is deterministic") {
    FunctionFixture fixture;
    IRBuilder builder(fixture.module.types());
    builder.set_insert_point(fixture.entry);
    auto* sum = builder.create_add(fixture.left, fixture.right, "sum");
    builder.create_ret(sum);

    std::ostringstream first;
    std::ostringstream second;
    print_module(fixture.module, first);
    print_module(fixture.module, second);
    CHECK(first.str() == second.str());
}

TEST_CASE("IR types and constants print structurally") {
    Module module(krc::target::TargetId::Kr32);
    const auto i32 = module.types().int_type(32);
    const auto pointer = module.types().pointer_type(i32);
    const auto array = module.types().array_type(i32, 3);
    CHECK(type_to_string(i32) == "i32");
    CHECK(type_to_string(pointer) == "ptr<i32>");
    CHECK(type_to_string(array) == "[3 x i32]");
    CHECK(type_to_string(module.types().bool_type()) == "i1");
    CHECK(type_to_string(module.constant_int(i32, 7)->type) == "i32");
}
