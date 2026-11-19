#include "doctest.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "ir/ir.h"
#include "ir/passes.h"

namespace {

// A function `i32 @pick(i32 %flag, i32 %a, i32 %b)` whose body the caller builds.
struct Fixture {
    krc::ir::Module module{krc::target::TargetId::Kr32};
    krc::ir::Function* function = nullptr;
    krc::ir::Parameter* flag = nullptr;
    krc::ir::Parameter* left = nullptr;
    krc::ir::Parameter* right = nullptr;

    Fixture() {
        const auto i32 = module.types().int_type(32);
        const auto type = module.types().function_type(
            i32, std::vector<krc::ir::TypePtr>{i32, i32, i32});
        function = module.add_function("pick", type, krc::ir::Linkage::External);
        flag = function->add_parameter(i32, "flag");
        left = function->add_parameter(i32, "left");
        right = function->add_parameter(i32, "right");
    }

    [[nodiscard]] bool verifies() {
        krc::ir::Verifier verifier;
        std::string error;
        return verifier.verify(module, error);
    }

    [[nodiscard]] std::string print() {
        std::string text;
        std::ostringstream out;
        krc::ir::print_module(module, out);
        return out.str();
    }

    [[nodiscard]] std::size_t count(const std::string& needle) {
        const auto text = print();
        std::size_t total = 0;
        for (auto at = text.find(needle); at != std::string::npos;
             at = text.find(needle, at + 1)) {
            ++total;
        }
        return total;
    }
};

// A slot stored once per arm and read at the join: the diamond that needs a phi.
void build_diamond(Fixture& fixture) {
    const auto i32 = fixture.module.types().int_type(32);
    auto* entry = fixture.function->add_block("entry");
    auto* take_left = fixture.function->add_block("take.left");
    auto* take_right = fixture.function->add_block("take.right");
    auto* join = fixture.function->add_block("join");

    krc::ir::IRBuilder builder(fixture.module.types());
    builder.set_insert_point(entry);
    auto* slot = builder.create_alloca(i32, 0, "slot");
    auto* condition = builder.create_icmp(krc::ir::ICmpPredicate::Ne, fixture.flag,
                                           fixture.module.constant_int(i32, 0), "cond");
    builder.create_cond_br(condition, take_left, take_right);

    builder.set_insert_point(take_left);
    builder.create_store(fixture.left, slot);
    builder.create_br(join);

    builder.set_insert_point(take_right);
    builder.create_store(fixture.right, slot);
    builder.create_br(join);

    builder.set_insert_point(join);
    auto* read = builder.create_load(i32, slot, false, 0, "read");
    builder.create_ret(read);
}

}  // namespace

TEST_CASE("Promoting a diamond leaves a phi and no memory traffic") {
    Fixture fixture;
    build_diamond(fixture);
    REQUIRE(fixture.verifies());

    const auto change = krc::ir::promote_cross_block_allocas(fixture.module);
    CHECK(change.instructions_removed == 4);  // alloca, two stores, one load

    CHECK(fixture.verifies());

    const auto text = fixture.print();
    CHECK(text.find("phi") != std::string::npos);
    CHECK(text.find("alloca") == std::string::npos);
    CHECK(text.find("= load") == std::string::npos);
    CHECK(text.find("store") == std::string::npos);
    // The two arms' values are what the join carries.
    CHECK(text.find("%left") != std::string::npos);
    CHECK(text.find("%right") != std::string::npos);
}

TEST_CASE("Promoting a loop puts the back edge in the phi") {
    Fixture fixture;
    const auto i32 = fixture.module.types().int_type(32);
    auto* entry = fixture.function->add_block("entry");
    auto* header = fixture.function->add_block("loop");
    auto* exit = fixture.function->add_block("exit");

    krc::ir::IRBuilder builder(fixture.module.types());
    builder.set_insert_point(entry);
    auto* slot = builder.create_alloca(i32, 0, "slot");
    builder.create_store(fixture.module.constant_int(i32, 0), slot);
    builder.create_br(header);

    builder.set_insert_point(header);
    auto* read = builder.create_load(i32, slot, false, 0, "read");
    auto* next = builder.create_add(read, fixture.module.constant_int(i32, 1), "next");
    builder.create_store(next, slot);
    auto* more = builder.create_icmp(krc::ir::ICmpPredicate::Ugt, next,
                                      fixture.module.constant_int(i32, 9), "more");
    builder.create_cond_br(more, exit, header);

    builder.set_insert_point(exit);
    auto* last = builder.create_load(i32, slot, false, 0, "last");
    builder.create_ret(last);

    REQUIRE(fixture.verifies());
    const auto change = krc::ir::promote_cross_block_allocas(fixture.module);
    CHECK(change.instructions_removed >= 5);
    CHECK(fixture.verifies());
    const auto text = fixture.print();
    CHECK(text.find("phi") != std::string::npos);
    CHECK(text.find("alloca") == std::string::npos);
    CHECK(text.find("= load") == std::string::npos);
}

TEST_CASE("A recorded variable is never promoted") {
    Fixture fixture;
    const auto i32 = fixture.module.types().int_type(32);
    auto* entry = fixture.function->add_block("entry");
    krc::ir::IRBuilder builder(fixture.module.types());
    builder.set_insert_point(entry);
    // IRGen's name for a source variable: the backend writes a V record for it.
    auto* slot = builder.create_alloca(i32, 0, "named.addr");
    builder.create_store(fixture.left, slot);
    auto* read = builder.create_load(i32, slot, false, 0, "read");
    builder.create_ret(read);

    REQUIRE(fixture.verifies());
    const auto change = krc::ir::promote_cross_block_allocas(fixture.module);
    CHECK(change.instructions_removed == 0);
    CHECK(fixture.print().find("alloca") != std::string::npos);
    CHECK(fixture.verifies());
}

TEST_CASE("An escaping address is never promoted") {
    Fixture fixture;
    const auto i32 = fixture.module.types().int_type(32);
    auto* entry = fixture.function->add_block("entry");
    krc::ir::IRBuilder builder(fixture.module.types());
    builder.set_insert_point(entry);
    auto* slot = builder.create_alloca(i32, 0, "slot");
    builder.create_store(fixture.left, slot);
    auto* element = builder.create_gep(i32, slot, {fixture.module.constant_int(i32, 0)},
                                       false, "element");
    auto* read = builder.create_load(i32, element, false, 0, "read");
    builder.create_ret(read);

    REQUIRE(fixture.verifies());
    const auto change = krc::ir::promote_cross_block_allocas(fixture.module);
    CHECK(change.instructions_removed == 0);
    CHECK(fixture.verifies());
}

TEST_CASE("A load no store reaches is never promoted") {
    Fixture fixture;
    const auto i32 = fixture.module.types().int_type(32);
    auto* entry = fixture.function->add_block("entry");
    auto* other = fixture.function->add_block("other");
    krc::ir::IRBuilder builder(fixture.module.types());
    builder.set_insert_point(entry);
    auto* slot = builder.create_alloca(i32, 0, "slot");
    auto* read = builder.create_load(i32, slot, false, 0, "read");
    builder.create_br(other);

    builder.set_insert_point(other);
    builder.create_store(fixture.left, slot);
    auto* second = builder.create_load(i32, slot, false, 0, "second");
    builder.create_ret(second);
    static_cast<void>(read);

    REQUIRE(fixture.verifies());
    const auto change = krc::ir::promote_cross_block_allocas(fixture.module);
    // The first block reads a slot nothing has written, so the promotion is
    // refused and the function is left exactly as it was.
    CHECK(change.instructions_removed == 0);
    CHECK(fixture.print().find("alloca") != std::string::npos);
    CHECK(fixture.verifies());
}

TEST_CASE("A critical edge is split before its phi is written") {
    // `then` has two successors and `join` has three predecessors, so the edge
    // then->join is critical: the phi copy has nowhere of its own to go until the
    // edge gets a block. This is the order the pipeline uses.
    Fixture fixture;
    const auto i32 = fixture.module.types().int_type(32);
    auto* entry = fixture.function->add_block("entry");
    auto* then_block = fixture.function->add_block("then");
    auto* else_block = fixture.function->add_block("else");
    auto* other = fixture.function->add_block("other");
    auto* join = fixture.function->add_block("join");

    krc::ir::IRBuilder builder(fixture.module.types());
    builder.set_insert_point(entry);
    auto* slot = builder.create_alloca(i32, 0, "slot");
    auto* first = builder.create_icmp(krc::ir::ICmpPredicate::Ne, fixture.flag,
                                       fixture.module.constant_int(i32, 0), "first");
    builder.create_cond_br(first, then_block, else_block);

    builder.set_insert_point(then_block);
    builder.create_store(fixture.left, slot);
    auto* second = builder.create_icmp(krc::ir::ICmpPredicate::Ne, fixture.flag,
                                        fixture.module.constant_int(i32, 1), "second");
    builder.create_cond_br(second, join, other);

    builder.set_insert_point(else_block);
    builder.create_store(fixture.right, slot);
    builder.create_br(join);

    builder.set_insert_point(other);
    builder.create_store(fixture.right, slot);
    builder.create_br(join);

    builder.set_insert_point(join);
    auto* read = builder.create_load(i32, slot, false, 0, "read");
    builder.create_ret(read);

    REQUIRE(fixture.verifies());
    const auto split = krc::ir::split_critical_edges(fixture.module);
    CHECK(split.edges_split >= 1);
    CHECK(fixture.verifies());
    const auto change = krc::ir::promote_cross_block_allocas(fixture.module);
    CHECK(change.instructions_removed >= 4);
    CHECK(fixture.verifies());
    CHECK(fixture.print().find("alloca") == std::string::npos);
}
