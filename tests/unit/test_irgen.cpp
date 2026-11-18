#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#include "asm/kr32_assembler.h"
#include "backend/kr32_backend.h"
#include "doctest.h"
#include "ir/ir.h"
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
