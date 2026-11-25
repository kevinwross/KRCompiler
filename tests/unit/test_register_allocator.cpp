#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "backend/register_allocator.h"
#include "backend/peephole.h"
#include "doctest.h"
#include "ir/ir.h"

using krc::ir::Function;
using krc::ir::IRBuilder;
using krc::ir::Linkage;
using krc::ir::Module;
using krc::ir::TypeContext;

namespace {

struct Fixture {
    Module module;
    Function* function = nullptr;
    TypeContext* types = nullptr;
    IRBuilder builder;

    Fixture() : types(&module.types()), builder(module.types()) {
        auto* function_type = types->function_type(types->int_type(32),
                                                   {types->int_type(32), types->int_type(32)},
                                                   false);
        function = module.add_function("f", function_type, Linkage::External, false);
        function->add_parameter(types->int_type(32), "a");
        function->add_parameter(types->int_type(32), "b");
        auto* entry = function->add_block("entry");
        builder.set_insert_point(entry);
    }
};

}  // namespace

TEST_CASE("graph-colouring allocator maps a two-colourable function") {
    Fixture fixture;
    auto* left = fixture.function->parameters[0].get();
    auto* right = fixture.function->parameters[1].get();
    auto* sum = fixture.builder.create_add(left, right, "sum");
    auto* total = fixture.builder.create_add(sum, left, "total");
    fixture.builder.create_ret(total);

    krc::backend::RegisterAllocation allocation;
    std::string error;
    REQUIRE_MESSAGE(krc::backend::allocate_registers(*fixture.function, {0, 1}, allocation,
                                                     error),
                    error);
    CHECK(allocation.spilled.empty());
    CHECK(allocation.registers.size() == 4);
    REQUIRE(allocation.registers.count(left) == 1);
    REQUIRE(allocation.registers.count(sum) == 1);
    CHECK(allocation.registers.at(left) != allocation.registers.at(sum));

    for (const auto& [value, reg] : allocation.registers) {
        static_cast<void>(value);
        CHECK(reg <= 1);
    }
}

TEST_CASE("graph-colouring allocator reports spills under pressure") {
    Fixture fixture;
    auto* left = fixture.function->parameters[0].get();
    auto* right = fixture.function->parameters[1].get();
    auto* third = fixture.builder.create_add(left, right, "third");
    auto* first_sum = fixture.builder.create_add(left, third, "first_sum");
    auto* second_sum = fixture.builder.create_add(left, right, "second_sum");
    auto* total = fixture.builder.create_add(first_sum, second_sum, "total");
    fixture.builder.create_ret(total);

    krc::backend::RegisterAllocation allocation;
    std::string error;
    REQUIRE_MESSAGE(krc::backend::allocate_registers(*fixture.function, {0}, allocation, error),
                    error);
    CHECK_FALSE(allocation.spilled.empty());
}

TEST_CASE("peephole removes identity and zero-strength operations") {
    const auto input = std::string("        MOV R0, R0\n"
                                   "        ADD R1, R1, #0\n"
                                   "        MUL R2, R2, #1\n"
                                   "        ADD R3, R3, #2\n"
                                   "        SHL R4, R4, #1\n");
    const auto output = krc::backend::peephole(input);
    CHECK(output.find("MOV R0, R0") == std::string::npos);
    CHECK(output.find("ADD R1, R1, #0") == std::string::npos);
    CHECK(output.find("MUL R2, R2, #1") == std::string::npos);
    CHECK(output.find("ADD R3, R3, #2") != std::string::npos);
    CHECK(output.find("SHL R4, R4, #1") != std::string::npos);
}

TEST_CASE("copy propagation removes a redundant move chain") {
    const auto input = std::string("        MOV R0, R13\n"
                                   ".Ldbg.f.0:\n"
                                   "        MOV R13, R0\n"
                                   "        MOV R14, R13\n"
                                   "        MOV R0, R14\n"
                                   "        CALL f\n"
                                   "        MOV R0, R13\n"
                                   "        RET\n");
    const auto output = krc::backend::propagate_copies(input);
    CHECK(output.find("MOV R13, R0") == std::string::npos);
    CHECK(output.find("MOV R0, R14") == std::string::npos);
    std::size_t kept = 0;
    for (auto at = output.find("MOV R0, R13"); at != std::string::npos;
         at = output.find("MOV R0, R13", at + 1)) {
        ++kept;
    }
    CHECK(kept == 2);
}

TEST_CASE("copy propagation invalidates an integer conversion destination") {
    const auto input = std::string("        MOV R13, R0\n"
                                   "        FCVT.W.D R0, F0\n"
                                   "        MOV R13, R0\n");
    const auto output = krc::backend::propagate_copies(input);
    std::size_t kept = 0;
    for (auto at = output.find("MOV R13, R0"); at != std::string::npos;
         at = output.find("MOV R13, R0", at + 1)) {
        ++kept;
    }
    CHECK(kept == 2);
}

TEST_CASE("store-to-load forwarding drops a frame reload") {
    const auto input = std::string("        STORE R0, [R7 + 104]\n"
                                   ".Ldbg.f.1:\n"
                                   "        LOAD R0, [R7 + 104]\n"
                                   "        STORE R1, [R7 + 108]\n"
                                   ".Lf.bb1:\n"
                                   "        LOAD R1, [R7 + 108]\n");
    const auto output = krc::backend::forward_store_loads(input);
    CHECK(output.find("LOAD R0, [R7 + 104]") == std::string::npos);
    CHECK(output.find("LOAD R1, [R7 + 108]") != std::string::npos);
    CHECK(output.find("STORE R0, [R7 + 104]") != std::string::npos);
}

TEST_CASE("store-to-load forwarding rewrites a cross-register reload") {
    const auto input = std::string("        STORE R0, [R7 + 104]\n"
                                   ".Ldbg.f.1:\n"
                                   "        LOAD R1, [R7 + 104]\n");
    const auto output = krc::backend::forward_store_loads(input);
    CHECK(output.find("LOAD R1, [R7 + 104]") == std::string::npos);
    CHECK(output.find("MOV R1, R0") != std::string::npos);
}

TEST_CASE("store-source coalescing removes a dead move into R0") {
    const auto input = std::string("        MOV R0, R15\n"
                                   "        STORE R0, [R7 + 4]\n"
                                   "        MOV R0, #1\n"
                                   "        MOV R0, R14\n"
                                   "        STORE R0, [R7 + 8]\n"
                                   "        MOV R1, R0\n");
    const auto output = krc::backend::coalesce_store_sources(input);
    CHECK(output.find("STORE R15, [R7 + 4]") != std::string::npos);
    CHECK(output.find("STORE R14, [R7 + 8]") == std::string::npos);
    CHECK(output.find("MOV R0, R14") != std::string::npos);
}

TEST_CASE("move-arithmetic combining folds the two-address form") {
    const auto input = std::string("        MOV R0, R8\n"
                                   ".Ldbg.f.1:\n"
                                   "        ADD R0, R0, #12\n"
                                   "        MOV R1, R9\n"
                                   "        ADD R2, R2, R1\n");
    const auto output = krc::backend::combine_move_arithmetic(input);
    CHECK(output.find("MOV R0, R8") == std::string::npos);
    CHECK(output.find("ADD R0, R8, #12") != std::string::npos);
    CHECK(output.find("MOV R1, R9") != std::string::npos);
}

TEST_CASE("move-arithmetic combining rewrites a repeated destination operand") {
    const auto input = std::string("        MOV R0, R8\n"
                                   "        ADD R0, R0, R0\n");
    const auto output = krc::backend::combine_move_arithmetic(input);
    CHECK(output.find("ADD R0, R8, R8") != std::string::npos);
}
