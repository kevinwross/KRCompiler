#include <string>
#include <utility>

#include "doctest.h"
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

struct Analyzed {
    bool had_errors = false;
    std::size_t warnings = 0;
};

Analyzed analyze(std::string source, krc::target::TargetId target = krc::target::TargetId::Kr32) {
    SourceManager sources;
    DiagnosticEngine diagnostics(sources);
    const auto file = sources.add_buffer("test.c", std::move(source));
    const auto& profile = krc::target::get_target(target);
    Preprocessor preprocessor(sources, diagnostics, profile);
    auto tokens = preprocessor.preprocess(file);
    Parser parser(std::move(tokens), diagnostics, sources);
    auto ast = parser.parse_translation_unit();
    if (ast && !parser.had_error()) {
        Sema sema(diagnostics, profile);
        sema.analyze(*ast);
    }
    return Analyzed{diagnostics.has_errors(), diagnostics.warning_count()};
}

}  // namespace

TEST_CASE("Sema accepts function calls and local variables") {
    const auto result = analyze(R"SRC(
int add(int a, int b) { return a + b; }
int main(void) {
    int value = add(1, 2);
    return value;
}
)SRC");
    CHECK_FALSE(result.had_errors);
}

TEST_CASE("Sema rejects an undeclared identifier") {
    const auto result = analyze("int main(void) { return missing; }\n");
    CHECK(result.had_errors);
}

TEST_CASE("Sema rejects a duplicate local declaration") {
    const auto result = analyze("int main(void) { int value; int value; return 0; }\n");
    CHECK(result.had_errors);
}

TEST_CASE("Sema resolves struct members") {
    const auto result = analyze(R"SRC(
struct Point { int x; int y; };
int main(void) {
    struct Point point;
    point.x = 1;
    return point.y;
}
)SRC");
    CHECK_FALSE(result.had_errors);
}

TEST_CASE("Sema rejects an unknown struct member") {
    const auto result = analyze(R"SRC(
struct Point { int x; };
int main(void) {
    struct Point point;
    return point.z;
}
)SRC");
    CHECK(result.had_errors);
}

TEST_CASE("Sema checks function argument counts") {
    const auto too_few = analyze("int f(int value); int main(void) { return f(); }\n");
    CHECK(too_few.had_errors);

    const auto too_many = analyze("int f(int value); int main(void) { return f(1, 2); }\n");
    CHECK(too_many.had_errors);
}

TEST_CASE("Sema accepts arrays and pointer arithmetic") {
    const auto result = analyze(R"SRC(
int main(void) {
    int values[3];
    int *pointer = values;
    pointer[0] = 1;
    return *pointer;
}
)SRC");
    CHECK_FALSE(result.had_errors);
}

TEST_CASE("Sema resolves enum constants and sizeof types") {
    const auto result = analyze(R"SRC(
struct Pair { char tag; int value; };
_Static_assert(sizeof(struct Pair) == 8, "pair layout");
enum Color { Red, Green = 3 };
int main(void) {
    return Green;
}
)SRC");
    CHECK_FALSE(result.had_errors);
}

TEST_CASE("Sema accepts __func__, __builtin_offsetof, and _Generic") {
    const auto result = analyze(R"SRC(
struct Aligned { char tag; int value; };
int main(void) {
    int value = _Generic(1, int: 4, default: 0);
    (void)__func__;
    return value + (int)__builtin_offsetof(struct Aligned, value);
}
)SRC");
    CHECK_FALSE(result.had_errors);
}

TEST_CASE("Sema computes struct layout, bit-fields, and offsetof") {
    const auto result = analyze(R"SRC(
struct Pair { char tag; int value; };
struct Bits { unsigned a : 1; unsigned b : 3; unsigned c : 28; };
_Static_assert(sizeof(struct Pair) == 8, "pair layout");
_Static_assert(__builtin_offsetof(struct Pair, value) == 4, "pair offset");
_Static_assert(sizeof(struct Bits) == 4, "bits layout");
_Static_assert(sizeof(int) == 4);
)SRC");
    CHECK_FALSE(result.had_errors);
}

TEST_CASE("Sema honors _Alignas in member layout") {
    const auto result = analyze(R"SRC(
struct Aligned { char tag; _Alignas(16) int value; };
_Static_assert(sizeof(struct Aligned) == 32, "aligned size");
_Static_assert(__builtin_offsetof(struct Aligned, value) == 16, "aligned offset");
)SRC");
    CHECK_FALSE(result.had_errors);
}

TEST_CASE("Sema rejects a failing static assertion") {
    const auto result = analyze("_Static_assert(sizeof(int) == 2, \"wrong width\");\n");
    CHECK(result.had_errors);
}

TEST_CASE("Sema rejects K&R function definitions") {
    const auto result = analyze("int f(a) { return a; }\n");
    CHECK(result.had_errors);
}

TEST_CASE("Sema rejects unsupported C11 features") {
    CHECK(analyze("_Atomic int value;\n").had_errors);
    CHECK(analyze("_Complex double value;\n").had_errors);
    CHECK(analyze("_Imaginary double value;\n").had_errors);
    CHECK(analyze("int f(int n) { int values[n]; return 0; }\n").had_errors);
    CHECK(analyze("void main(void) { }\n").had_errors);
    CHECK(analyze("int main(void) { typeof(1) value; return 0; }\n").had_errors);
}

TEST_CASE("Sema rejects implicit function declarations on KR32") {
    const auto result = analyze("int main(void) { return puts(\"hello\"); }\n");
    CHECK(result.had_errors);
}

TEST_CASE("Sema allows implicit functions on KR16 with a warning") {
    const auto result = analyze("int main(void) { return puts(\"hello\"); }\n",
                                krc::target::TargetId::Kr16);
    CHECK_FALSE(result.had_errors);
    CHECK(result.warnings == 1);
}
