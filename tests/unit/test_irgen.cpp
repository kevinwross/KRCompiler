#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#include "asm/kr32_assembler.h"
#include "backend/kr16_backend.h"
#include "backend/kr32_backend.h"
#include "doctest.h"
#include "ir/ir.h"
#include "ir/passes.h"
#include "irgen/c_irgen.h"
#include "krc/frontend/diagnostics.h"
#include "krc/frontend/parser.h"
#include "krc/frontend/preprocessor.h"
#include "krc/frontend/sema.h"
#include "krc/frontend/source_manager.h"
#include "krc/target/target.h"

using krc::frontend::DiagnosticEngine;
using krc::frontend::Parser;
using krc::frontend::Preprocessor;
using krc::frontend::Sema;
using krc::frontend::SourceManager;

namespace {

struct Generated {
    std::unique_ptr<krc::ir::Module> module;
    bool had_errors = false;
    std::string verifier_error;
};

// The language comes from the declaration site rather than a bool at each call,
// because the front end selects it from the input extension: `generate` is the C
// path, `generate_cxx` the C++ one.
Generated generate_in(std::string source, bool cxx) {
    SourceManager sources;
    DiagnosticEngine diagnostics(sources);
    const auto file = sources.add_buffer(cxx ? "test.cpp" : "test.c", std::move(source));
    const auto& profile = krc::target::get_target(krc::target::TargetId::Kr32);
    Preprocessor preprocessor(sources, diagnostics, profile);
    auto tokens = preprocessor.preprocess(file);
    Parser parser(std::move(tokens), diagnostics, sources, cxx);
    auto ast = parser.parse_translation_unit();
    if (!ast || parser.had_error()) {
        return Generated{nullptr, diagnostics.has_errors(), {}};
    }
    Sema sema(diagnostics, profile, cxx);
    sema.analyze(*ast);
    if (diagnostics.has_errors()) {
        return Generated{nullptr, true, {}};
    }
    krc::irgen::CIRGen irgen(sema, *ast, profile, diagnostics);
    auto module = irgen.generate();
    std::string verifier_error;
    if (module) {
        krc::ir::Verifier verifier;
        if (!verifier.verify(*module, verifier_error)) {
            return Generated{std::move(module), true, std::move(verifier_error)};
        }
    }
    return Generated{std::move(module), diagnostics.has_errors(), std::move(verifier_error)};
}

Generated generate(std::string source) {
    return generate_in(std::move(source), false);
}

Generated generate_cxx(std::string source) {
    return generate_in(std::move(source), true);
}

}  // namespace

TEST_CASE("C IRGen lowers a scalar function") {
    const auto generated = generate(R"SRC(
int add(int left, int right) {
    return left + right;
}
int main(void) {
    return add(1, 2);
}
)SRC");
    CHECK_FALSE(generated.had_errors);
    REQUIRE(generated.module);
    CHECK(generated.verifier_error.empty());
    CHECK(generated.module->functions().size() == 2);

    std::ostringstream out;
    krc::ir::print_module(*generated.module, out);
    const auto text = out.str();
    CHECK(text.find("define i32 @add(i32 %left, i32 %right)") != std::string::npos);
    CHECK(text.find("call i32 @add(i32 1, i32 2)") != std::string::npos);
}

TEST_CASE("C IRGen lowers the hello program to verified IR") {
    const auto generated = generate(R"SRC(
int putchar(int c);
void exit(int code);

int main(void) {
    char *message = "Hello, KR32!\n";
    while (*message) {
        putchar(*message);
        message = message + 1;
    }
    return 0;
}
)SRC");
    CHECK_FALSE(generated.had_errors);
    REQUIRE(generated.module);
    CHECK(generated.verifier_error.empty());
    CHECK(generated.module->globals().size() == 1);

    std::ostringstream out;
    krc::ir::print_module(*generated.module, out);
    const auto text = out.str();
    CHECK(text.find("declare i32 @putchar(i32)") != std::string::npos);
    CHECK(text.find("@.str.0 = constant") != std::string::npos);
    CHECK(text.find("define i32 @main()") != std::string::npos);
    CHECK(text.find("while.cond") != std::string::npos);
    CHECK(text.find("call i32 @putchar") != std::string::npos);

    const auto& profile = krc::target::get_target(krc::target::TargetId::Kr32);
    krc::backend::Kr32Backend backend(*generated.module, profile);
    std::string assembly_error;
    const auto assembly = backend.emit(assembly_error);
    REQUIRE(assembly);
    CHECK(assembly_error.empty());
    CHECK(assembly->find("PUSH R7") != std::string::npos);
    CHECK(assembly->find("CALL putchar") != std::string::npos);
    CHECK(assembly->find(".global main") != std::string::npos);
    CHECK(assembly->find("HALT ; unsupported") == std::string::npos);

    krc::asmkr32::AssemblyResult assembled;
    std::string assemble_error;
    REQUIRE_MESSAGE(krc::asmkr32::assemble(*assembly, assembled, assemble_error),
                    assemble_error);
    CHECK_FALSE(assembled.text.empty());
    CHECK_FALSE(assembled.data.empty());
    CHECK(assembled.relocations.size() >= 9);
    const auto has_symbol = [&](std::string_view symbol) {
        return std::any_of(assembled.relocations.begin(), assembled.relocations.end(),
                           [&](const krc::asmkr32::AssembledRelocation& relocation) {
                               return relocation.symbol == symbol;
                           });
    };
    CHECK(has_symbol(".str.0"));
    CHECK(has_symbol("putchar"));
}

TEST_CASE("C IRGen lowers structs and recursive pointers") {
    const auto generated = generate(R"SRC(
struct Node {
    int value;
    struct Node *next;
};
int main(void) {
    struct Node node;
    node.value = 5;
    node.next = 0;
    return node.value;
}
)SRC");
    CHECK_FALSE(generated.had_errors);
    REQUIRE(generated.module);
    CHECK(generated.verifier_error.empty());

    const auto& profile = krc::target::get_target(krc::target::TargetId::Kr32);
    krc::backend::Kr32Backend backend(*generated.module, profile);
    std::string assembly_error;
    const auto assembly = backend.emit(assembly_error);
    REQUIRE(assembly);
    CHECK(assembly_error.empty());
    // `node.next` sits at byte offset 4, so the member offset is carried by the
    // ADD itself rather than materialised into R1 and scaled by the stride.
    CHECK(assembly->find("ADD R0, R0, #4") != std::string::npos);
    CHECK(assembly->find("MOV R1, #4") == std::string::npos);
    CHECK(assembly->find("HALT ; unsupported") == std::string::npos);

    krc::asmkr32::AssemblyResult assembled;
    std::string assemble_error;
    REQUIRE_MESSAGE(krc::asmkr32::assemble(*assembly, assembled, assemble_error),
                    assemble_error);
    CHECK_FALSE(assembled.text.empty());
}

TEST_CASE("KR32 backend carries a constant operand in the instruction") {
    const auto generated = generate(R"SRC(
int add_seven(int value) { return 7 + value; }
int sub_five(int value) { return value - 5; }
int less_seven(int value) { return value < 7; }
)SRC");
    CHECK_FALSE(generated.had_errors);
    REQUIRE(generated.module);
    CHECK(generated.verifier_error.empty());

    const auto& profile = krc::target::get_target(krc::target::TargetId::Kr32);
    krc::backend::Kr32Backend backend(*generated.module, profile);
    std::string assembly_error;
    const auto assembly = backend.emit(assembly_error);
    REQUIRE(assembly);
    CHECK(assembly_error.empty());

    CHECK(assembly->find("ADD R0, R0, #7") != std::string::npos);
    CHECK(assembly->find("SUB R0, R0, #5") != std::string::npos);
    CHECK(assembly->find("CMP R0, #7") != std::string::npos);
    // 7 + value puts the literal on the left; the op is commutative, so it
    // becomes the instruction's literal operand and never reaches a register.
    CHECK(assembly->find("MOV R0, #7") == std::string::npos);
    CHECK(assembly->find("MOV R1, #5") == std::string::npos);
}

TEST_CASE("KR32 backend keeps a register operand for a constant outside the inline field") {
    const auto generated = generate(R"SRC(
int at_limit(int value) { return value + 1023; }
int past_limit(int value) { return value + 1024; }
)SRC");
    CHECK_FALSE(generated.had_errors);
    REQUIRE(generated.module);
    CHECK(generated.verifier_error.empty());

    const auto& profile = krc::target::get_target(krc::target::TargetId::Kr32);
    krc::backend::Kr32Backend backend(*generated.module, profile);
    std::string assembly_error;
    const auto assembly = backend.emit(assembly_error);
    REQUIRE(assembly);
    CHECK(assembly_error.empty());

    // 1023 is the largest value the eleven-bit inline field holds.
    CHECK(assembly->find("ADD R0, R0, #1023") != std::string::npos);
    CHECK(assembly->find("MOV R1, #1024") != std::string::npos);
}

TEST_CASE("KR32 backend folds a constant element index into the address") {
    const auto generated = generate(R"SRC(
int third(int *values) { return values[3]; }
int member(struct Pair { int first; int second; } *pair) { return pair->second; }
int walk(int *values, int index) { return values[index]; }
)SRC");
    CHECK_FALSE(generated.had_errors);
    REQUIRE(generated.module);
    CHECK(generated.verifier_error.empty());

    const auto& profile = krc::target::get_target(krc::target::TargetId::Kr32);
    krc::backend::Kr32Backend backend(*generated.module, profile);
    std::string assembly_error;
    const auto assembly = backend.emit(assembly_error);
    REQUIRE(assembly);
    CHECK(assembly_error.empty());

    // values[3] is 3 * sizeof(int) bytes from the base, so the index never
    // reaches a register and no stride multiply is emitted for it.
    CHECK(assembly->find("ADD R0, R0, #12") != std::string::npos);
    CHECK(assembly->find("ADD R0, R0, #4") != std::string::npos);
    // A computed index still scales in a register; the fold is for literals.
    CHECK(assembly->find("MUL R1, R1, #4") != std::string::npos);
}

TEST_CASE("KR32 backend branches on a comparison without materialising a boolean") {
    const auto generated = generate(R"SRC(
int classify(int value) {
    if (value < 5) return 1;
    return 2;
}
)SRC");
    CHECK_FALSE(generated.had_errors);
    REQUIRE(generated.module);
    CHECK(generated.verifier_error.empty());

    const auto& profile = krc::target::get_target(krc::target::TargetId::Kr32);
    krc::backend::Kr32Backend backend(*generated.module, profile);
    std::string assembly_error;
    const auto assembly = backend.emit(assembly_error);
    REQUIRE(assembly);
    CHECK(assembly_error.empty());

    // The comparison reaches the branch directly, so no boolean is built in R2
    // and stored to a slot.
    CHECK(assembly->find("CMP R0, #5") != std::string::npos);
    CHECK(assembly->find(".icmp.") == std::string::npos);
}

TEST_CASE("KR32 backend materialises a comparison that another use reads") {
    const auto generated = generate(R"SRC(
int reuse(int left, int right) {
    int compared = left < right;
    return compared + compared;
}
)SRC");
    CHECK_FALSE(generated.had_errors);
    REQUIRE(generated.module);
    CHECK(generated.verifier_error.empty());

    const auto& profile = krc::target::get_target(krc::target::TargetId::Kr32);
    krc::backend::Kr32Backend backend(*generated.module, profile);
    std::string assembly_error;
    const auto assembly = backend.emit(assembly_error);
    REQUIRE(assembly);
    CHECK(assembly_error.empty());

    // Two uses: the value is needed beyond the branch, so the boolean survives.
    CHECK(assembly->find(".icmp.") != std::string::npos);
}

TEST_CASE("KR32 backend does not fold a 64-bit comparison into a branch") {
    const auto generated = generate(R"SRC(
int wide(long long left, long long right) {
    if (left < right) return 1;
    return 2;
}
)SRC");
    CHECK_FALSE(generated.had_errors);
    REQUIRE(generated.module);
    CHECK(generated.verifier_error.empty());

    const auto& profile = krc::target::get_target(krc::target::TargetId::Kr32);
    krc::backend::Kr32Backend backend(*generated.module, profile);
    std::string assembly_error;
    const auto assembly = backend.emit(assembly_error);
    REQUIRE(assembly);
    CHECK(assembly_error.empty());

    // A 64-bit comparison is a sequence over the register pair, so its flags are
    // not the ones a conditional branch tests: the materialised form stays.
    CHECK(assembly->find(".icmp64.") != std::string::npos);
}

TEST_CASE("KR32 backend addresses a local without a LEA") {
    const auto generated = generate(R"SRC(
int local(int value) {
    int stored = value + 1;
    return stored;
}
)SRC");
    CHECK_FALSE(generated.had_errors);
    REQUIRE(generated.module);
    CHECK(generated.verifier_error.empty());

    const auto& profile = krc::target::get_target(krc::target::TargetId::Kr32);
    krc::backend::Kr32Backend backend(*generated.module, profile);
    std::string assembly_error;
    const auto assembly = backend.emit(assembly_error);
    REQUIRE(assembly);
    CHECK(assembly_error.empty());

    // The local's slot is addressed by the load and the store themselves, so no
    // LEA computes its address and no instruction loads it indirectly.
    CHECK(assembly->find("LOAD R0, [R7 + ") != std::string::npos);
    CHECK(assembly->find("LEA R0, [R7 + 8]\n        LOAD R0, [R0]") == std::string::npos);
}

TEST_CASE("C IRGen initialises a record from an overloaded operator result") {
    const auto generated = generate_cxx(R"SRC(
struct Value {
    int amount;
    Value operator+(const Value& other) const {
        Value result;
        result.amount = amount + other.amount;
        return result;
    }
};
int total(void) {
    Value left;
    Value right;
    left.amount = 2;
    right.amount = 3;
    Value sum = left + right;
    return sum.amount;
}
)SRC");
    CHECK_FALSE(generated.had_errors);
    REQUIRE(generated.module);
    // An operator call is a binary expression, not a call expression; before the
    // temporary was materialised this reported "expression is not an lvalue" and
    // left the module failing the verifier.
    CHECK(generated.verifier_error.empty());
}

TEST_CASE("-O1 removes a branch to the label that follows it") {
    // The shape every function tail has: a jump to the epilogue label, which is
    // the next thing emitted. The jump is dead and the label stays.
    const std::string tail =
        "        JMP .Lmain.ret\n"
        ".Ldbg.main.7:\n"
        ".Lmain.ret:\n"
        "        RET\n";
    const auto optimized = krc::backend::remove_redundant_jumps(tail);
    CHECK(optimized.find("JMP") == std::string::npos);
    CHECK(optimized.find(".Lmain.ret:") != std::string::npos);
    CHECK(optimized.find("RET") != std::string::npos);

    // A jump to a label that is not next is live, and so is a conditional one.
    const std::string live =
        "        JMP .Lmain.ret\n"
        "        MOV R0, #1\n"
        ".Lmain.ret:\n"
        "        RET\n";
    CHECK(krc::backend::remove_redundant_jumps(live).find("JMP .Lmain.ret") !=
          std::string::npos);
    const std::string conditional =
        "        JNZ .Lcond\n"
        ".Lcond:\n"
        "        RET\n";
    CHECK(krc::backend::remove_redundant_jumps(conditional).find("JNZ") !=
          std::string::npos);

    // KR16's unconditional branch is `BRA`, and its register form has no label.
    const std::string kr16 =
        "    BRA .Lkr16.ret\n"
        ".Lkr16.ret:\n"
        "    RET\n";
    CHECK(krc::backend::remove_redundant_jumps(kr16).find("BRA") == std::string::npos);
    CHECK(krc::backend::remove_redundant_jumps("    JMP R3\n").find("JMP R3") !=
          std::string::npos);
}

namespace {

// A diamond whose join reads a `phi`. IRGen never builds one — it stages such
// values through an alloca — so this is hand-built to exercise the lowering the
// pass layer will depend on (docs/optimize.md E6).
void build_phi_diamond(krc::ir::Module& module) {
    const auto i32 = module.types().int_type(32);
    const auto i1 = module.types().int_type(1);
    const auto type = module.types().function_type(
        i32, std::vector<krc::ir::TypePtr>{i32, i32, i32});
    auto* function = module.add_function("pick", type, krc::ir::Linkage::External);
    auto* flag = function->add_parameter(i32, "flag");
    auto* left = function->add_parameter(i32, "left");
    auto* right = function->add_parameter(i32, "right");
    auto* entry = function->add_block("entry");
    auto* take_left = function->add_block("take.left");
    auto* take_right = function->add_block("take.right");
    auto* join = function->add_block("join");

    krc::ir::IRBuilder builder(module.types());
    builder.set_insert_point(entry);
    auto* condition =
        builder.create_icmp(krc::ir::ICmpPredicate::Ne, flag, module.constant_int(i32, 0), "cond");
    builder.create_cond_br(condition, take_left, take_right);

    builder.set_insert_point(take_left);
    builder.create_br(join);

    builder.set_insert_point(take_right);
    builder.create_br(join);

    builder.set_insert_point(join);
    auto* result = builder.create_phi(i32, "result");
    result->type = i32;
    result->incoming.push_back({left, take_left});
    result->incoming.push_back({right, take_right});
    builder.create_ret(result);
    static_cast<void>(i1);
}

}  // namespace

TEST_CASE("A phi's value is written on the incoming edges") {
    // A phi has no code of its own. Each predecessor must store its incoming
    // value before branching, and the join must read the slot the edges wrote.
    for (const auto target : {krc::target::TargetId::Kr32, krc::target::TargetId::Kr16}) {
        krc::ir::Module module(target);
        build_phi_diamond(module);
        const auto& profile = krc::target::get_target(target);

        std::string assembly_error;
        std::optional<std::string> assembly;
        if (target == krc::target::TargetId::Kr32) {
            krc::backend::Kr32Backend backend(module, profile);
            assembly = backend.emit(assembly_error);
        } else {
            krc::backend::Kr16Backend backend(module, profile);
            assembly = backend.emit(assembly_error);
        }
        REQUIRE_MESSAGE(assembly, assembly_error);
        CHECK(assembly_error.empty());
        CHECK(assembly->find("phi on a critical edge") == std::string::npos);
        CHECK(assembly->find("HALT") == std::string::npos);
        // Both arms carry a store into the phi's slot before their branch. The
        // two targets spell a store differently, so the check accepts either.
        for (const std::string arm : {".Lpick.take.left:", ".Lpick.take.right:"}) {
            const auto start = assembly->find(arm);
            REQUIRE(start != std::string::npos);
            const auto end = assembly->find("\n.L", start + arm.size());
            const auto body = assembly->substr(start, end - start);
            const bool stored = body.find("STORE R0, [") != std::string::npos ||
                                body.find("ST [SP+") != std::string::npos;
            CHECK(stored);
        }
    }
}

TEST_CASE("Empty-block elimination removes an empty arm and keeps an EH block") {
    const auto generated = generate(R"SRC(
int choose(int a, int b) {
    int result = 0;
    if (a > b) { result = 1; }
    return result;
}
)SRC");
    CHECK_FALSE(generated.had_errors);
    REQUIRE(generated.module);
    CHECK(generated.verifier_error.empty());

    const auto blocks_before = [&] {
        std::size_t total = 0;
        for (const auto& function : generated.module->functions()) {
            if (function) {
                total += function->blocks.size();
            }
        }
        return total;
    }();
    const auto change = krc::ir::empty_block_elimination(*generated.module);
    const auto blocks_after = [&] {
        std::size_t total = 0;
        for (const auto& function : generated.module->functions()) {
            if (function) {
                total += function->blocks.size();
            }
        }
        return total;
    }();
    // The empty else arm is a block whose only instruction is the branch.
    CHECK(blocks_after < blocks_before);
    CHECK(change.instructions_removed == blocks_before - blocks_after);

    krc::ir::Verifier verifier;
    std::string verify_error;
    CHECK(verifier.verify(*generated.module, verify_error));

    // A landing pad is never a jump to a jump. The count is unchanged by the
    // pass, and the module still verifies.
    const auto guarded = generate_cxx(R"SRC(
struct Error {
    int code;
};
int guarded(int value) {
    try {
        if (value < 0) {
            Error raised;
            raised.code = value;
            throw raised;
        }
        return value;
    } catch (Error& caught) {
        return caught.code;
    }
}
)SRC");
    CHECK_FALSE(guarded.had_errors);
    REQUIRE(guarded.module);
    const auto count_pads = [&] {
        std::size_t total = 0;
        for (const auto& function : guarded.module->functions()) {
            if (!function) {
                continue;
            }
            for (const auto& block : function->blocks) {
                if (!block) {
                    continue;
                }
                for (const auto& instruction : block->instructions) {
                    if (instruction && instruction->opcode == krc::ir::Opcode::LandingPad) {
                        ++total;
                    }
                }
            }
        }
        return total;
    }();
    static_cast<void>(krc::ir::empty_block_elimination(*guarded.module));
    std::size_t pads_after = 0;
    for (const auto& function : guarded.module->functions()) {
        if (!function) {
            continue;
        }
        for (const auto& block : function->blocks) {
            if (!block) {
                continue;
            }
            for (const auto& instruction : block->instructions) {
                if (instruction && instruction->opcode == krc::ir::Opcode::LandingPad) {
                    ++pads_after;
                }
            }
        }
    }
    CHECK(pads_after == count_pads);
    CHECK(verifier.verify(*guarded.module, verify_error));
}

TEST_CASE("Alloca promotion refuses a named variable and an escaping temporary") {
    // `named` is a source variable: its alloca is `<name>.addr` and carries a V
    // record, so promoting it would leave the debugger pointing at a dead slot.
    // `&named` also makes the address escape, which is refused independently —
    // this asserts the count is zero rather than which of the two rules fired.
    const auto generated = generate(R"SRC(
int named_local(int a) {
    int named = a + 1;
    int *taken = &named;
    return *taken;
}
)SRC");
    CHECK_FALSE(generated.had_errors);
    REQUIRE(generated.module);
    CHECK(generated.verifier_error.empty());

    const auto change = krc::ir::alloca_promotion(*generated.module);
    CHECK(change.instructions_removed == 0);

    krc::ir::Verifier verifier;
    std::string verify_error;
    CHECK(verifier.verify(*generated.module, verify_error));
}

TEST_CASE("Copy propagation never merges two loads of one address") {
    // `*p + *p` reads the same address twice. Two loads are two reads — a store
    // could sit between them — so this pass must leave both, and the adds that
    // consume them are not shareable either because their operands differ.
    const auto generated = generate(R"SRC(
int twice(int *pointer) { return *pointer + *pointer; }
)SRC");
    CHECK_FALSE(generated.had_errors);
    REQUIRE(generated.module);
    CHECK(generated.verifier_error.empty());

    const auto change = krc::ir::copy_propagation(*generated.module);
    CHECK(change.instructions_removed == 0);

    std::ostringstream out;
    krc::ir::print_module(*generated.module, out);
    const auto text = out.str();
    // Both reads survive: count the i32 loads rather than a generated name.
    std::size_t loads = 0;
    for (std::size_t at = text.find("= load i32"); at != std::string::npos;
         at = text.find("= load i32", at + 1)) {
        ++loads;
    }
    CHECK(loads == 2);

    krc::ir::Verifier verifier;
    std::string verify_error;
    CHECK(verifier.verify(*generated.module, verify_error));
}

TEST_CASE("Dead code elimination removes an unused pure value and keeps effects") {
    const auto generated = generate(R"SRC(
int unused_then_used(int value) {
    value + 1;
    value * 2;
    return value;
}
)SRC");
    CHECK_FALSE(generated.had_errors);
    REQUIRE(generated.module);
    CHECK(generated.verifier_error.empty());

    const auto before = [&] {
        std::size_t total = 0;
        for (const auto& function : generated.module->functions()) {
            for (const auto& block : function->blocks) {
                total += block->instructions.size();
            }
        }
        return total;
    }();
    const auto change = krc::ir::dead_code_elimination(*generated.module);
    // Two discarded expressions, and whatever fed them.
    CHECK(change.instructions_removed >= 2);
    std::size_t after = 0;
    for (const auto& function : generated.module->functions()) {
        for (const auto& block : function->blocks) {
            after += block->instructions.size();
        }
    }
    CHECK(after == before - change.instructions_removed);
    // The store of the parameter and the return are effects, not dead values.
    std::ostringstream printed;
    krc::ir::print_module(*generated.module, printed);
    CHECK(printed.str().find("ret i32") != std::string::npos);

    krc::ir::Verifier verifier;
    std::string verify_error;
    CHECK(verifier.verify(*generated.module, verify_error));
}

TEST_CASE("Constant folding replaces a folded operation with its value") {
    const auto generated = generate(R"SRC(
int folded(int *values) { return (2 + 3) * 4 + values[-1]; }
)SRC");
    CHECK_FALSE(generated.had_errors);
    REQUIRE(generated.module);
    CHECK(generated.verifier_error.empty());

    const auto change = krc::ir::constant_folding(*generated.module);
    CHECK(change.instructions_folded >= 2);
    CHECK(change.instructions_removed >= 2);

    std::ostringstream out;
    krc::ir::print_module(*generated.module, out);
    const auto text = out.str();
    // The folded add is gone, and the un-folded `sub i32 0, 1` that used to feed
    // the index became the literal the backend carries in the GEP.
    CHECK(text.find("add i32 2, 3") == std::string::npos);
    CHECK(text.find("getelementptr i32, %values.load, -1") != std::string::npos);

    // Rewriting operands is what makes a fold dangerous, so the pass's output is
    // verified rather than assumed.
    krc::ir::Verifier verifier;
    std::string verify_error;
    CHECK(verifier.verify(*generated.module, verify_error));
}

TEST_CASE("C IRGen lowers mainentry arguments and a for loop") {
    const auto generated = generate(R"SRC(
int printf(const char *format, ...);

int main(int argc, char *argv[]) {
    printf("argc = %d\n", argc);
    for (int index = 0; index < argc; ++index) {
        printf("argv[%d] = %s\n", index, argv[index]);
    }
    return 0;
}
)SRC");
    CHECK_FALSE(generated.had_errors);
    REQUIRE(generated.module);
    CHECK(generated.verifier_error.empty());

    std::ostringstream out;
    krc::ir::print_module(*generated.module, out);
    const auto text = out.str();
    CHECK(text.find("declare i32 @printf(ptr<i8>, ...)") != std::string::npos);
    CHECK(text.find("define i32 @main(i32 %argc, ptr<ptr<i8>> %argv)") != std::string::npos);
    CHECK(text.find("for.cond") != std::string::npos);
    CHECK(text.find("getelementptr ptr<i8>") != std::string::npos);

    const auto& profile = krc::target::get_target(krc::target::TargetId::Kr32);
    krc::backend::Kr32Backend backend(*generated.module, profile);
    std::string assembly_error;
    const auto assembly = backend.emit(assembly_error);
    REQUIRE(assembly);
    CHECK(assembly_error.empty());
    krc::asmkr32::AssemblyResult assembled;
    std::string assemble_error;
    REQUIRE_MESSAGE(krc::asmkr32::assemble(*assembly, assembled, assemble_error),
                    assemble_error);
    CHECK_FALSE(assembled.text.empty());
    CHECK(assembled.relocations.size() >= 12);
    const auto has_symbol = [&](std::string_view symbol) {
        return std::any_of(assembled.relocations.begin(), assembled.relocations.end(),
                           [&](const krc::asmkr32::AssembledRelocation& relocation) {
                               return relocation.symbol == symbol;
                           });
    };
    CHECK(has_symbol("printf"));
}
