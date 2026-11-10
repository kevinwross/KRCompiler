#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "doctest.h"
#include "krc/frontend/ast.h"
#include "krc/frontend/diagnostics.h"
#include "krc/frontend/parser.h"
#include "krc/frontend/preprocessor.h"
#include "krc/frontend/source_manager.h"
#include "krc/target/target.h"

using krc::frontend::AstKind;
using krc::frontend::DiagnosticEngine;
using krc::frontend::Parser;
using krc::frontend::Preprocessor;
using krc::frontend::SourceManager;

namespace {

struct Parsed {
    std::unique_ptr<krc::frontend::AstNode> root;
    bool had_errors = false;
};

Parsed parse(std::string source) {
    SourceManager sources;
    DiagnosticEngine diagnostics(sources);
    const auto file = sources.add_buffer("test.c", std::move(source));
    Preprocessor preprocessor(sources, diagnostics,
                              krc::target::get_target(krc::target::TargetId::Kr32));
    auto tokens = preprocessor.preprocess(file);
    Parser parser(std::move(tokens), diagnostics, sources);
    auto root = parser.parse_translation_unit();
    return Parsed{std::move(root), diagnostics.has_errors()};
}

}  // namespace

TEST_CASE("parser accepts a simple function definition") {
    const auto result = parse("int main(void) { return 0; }\n");
    CHECK_FALSE(result.had_errors);
    REQUIRE(result.root);
    REQUIRE(result.root->children.size() == 1);
    CHECK(result.root->children[0]->kind == AstKind::FunctionDefinition);
    CHECK(result.root->children[0]->text == "main");
}

TEST_CASE("parser accepts the KR32 hello world program") {
    const auto result = parse(R"SRC(
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
    CHECK_FALSE(result.had_errors);
    REQUIRE(result.root);
    CHECK(result.root->children.size() == 3);
    CHECK(result.root->children[0]->kind == AstKind::Declaration);
    CHECK(result.root->children[2]->kind == AstKind::FunctionDefinition);
}

TEST_CASE("parser accepts typedefs, structs, enums, and initializers") {
    const auto result = parse(R"SRC(
typedef struct Point { int x; int y; } Point;
Point p = {1, 2};
enum Color { Red, Green = 2 };
int main(void) {
    struct Point q;
    q.x = p.y;
    return q.x;
}
)SRC");
    CHECK_FALSE(result.had_errors);
    REQUIRE(result.root);
    CHECK(result.root->children.size() == 4);
}

TEST_CASE("parser accepts control flow and compound assignments") {
    const auto result = parse(R"SRC(
int f(int n) {
    int total = 0;
    for (int i = 0; i < n; i++) {
        if (i % 2 == 0) {
            total += i;
        } else {
            total -= i;
        }
    }
    do {
        total++;
    } while (total < 0);
    switch (total) {
    case 0:
        break;
    default:
        return total;
    }
    return total;
}
)SRC");
    CHECK_FALSE(result.had_errors);
    REQUIRE(result.root);
    REQUIRE(result.root->children.size() == 1);
    CHECK(result.root->children[0]->kind == AstKind::FunctionDefinition);
}

TEST_CASE("parser accepts arrays, function pointers, and calls") {
    const auto result = parse(R"SRC(
int (*fp)(int);
int a[3] = {1, 2, 3};
int main(void) {
    return fp(a[0]);
}
)SRC");
    CHECK_FALSE(result.had_errors);
    REQUIRE(result.root);
    CHECK(result.root->children.size() == 3);
}

TEST_CASE("parser accepts pointer-returning function definitions") {
    const auto result = parse("struct Node *new_node(void) { return 0; }\n");
    CHECK_FALSE(result.had_errors);
    REQUIRE(result.root);
    REQUIRE(result.root->children.size() == 1);
    CHECK(result.root->children[0]->kind == AstKind::FunctionDefinition);
    CHECK(result.root->children[0]->text == "new_node");
}

TEST_CASE("parser reports a missing semicolon") {
    const auto result = parse("int x = 1\nint main(void) { return x; }\n");
    CHECK(result.had_errors);
}

TEST_CASE("AST dump names the function") {
    const auto result = parse("int main(void) { return 0; }\n");
    REQUIRE(result.root);
    std::ostringstream out;
    krc::frontend::dump_ast(*result.root, out);
    CHECK(out.str().find("FunctionDefinition 'main'") != std::string::npos);
}
