#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "doctest.h"
#include "krc/frontend/diagnostics.h"
#include "krc/frontend/preprocessor.h"
#include "krc/frontend/source_manager.h"
#include "krc/target/target.h"

using krc::frontend::DiagnosticEngine;
using krc::frontend::Preprocessor;
using krc::frontend::SourceManager;
using krc::frontend::Token;

namespace {

struct Preprocessed {
    std::vector<Token> tokens;
    bool had_errors = false;
};

Preprocessed preprocess(std::string source,
                        std::vector<std::filesystem::path> include_paths = {}) {
    SourceManager sources;
    DiagnosticEngine diagnostics(sources);
    const auto file = sources.add_buffer("test.c", std::move(source));
    Preprocessor::Options options;
    options.include_paths = std::move(include_paths);
    Preprocessor processor(sources, diagnostics, krc::target::get_target(krc::target::TargetId::Kr32),
                           std::move(options));
    auto tokens = processor.preprocess(file);
    return Preprocessed{std::move(tokens), diagnostics.has_errors()};
}

std::string text_of(const std::vector<Token>& tokens) {
    std::string result;
    for (const auto& token : tokens) {
        if (!result.empty()) {
            result.push_back(' ');
        }
        result += token.text;
    }
    return result;
}

}  // namespace

TEST_CASE("preprocessor expands object and function macros") {
    const auto object = preprocess("#define VALUE 42\nint x = VALUE;\n");
    CHECK_FALSE(object.had_errors);
    CHECK(text_of(object.tokens) == "int x = 42 ;");

    const auto function = preprocess("#define ADD(a, b) ((a) + (b))\nint x = ADD(1, 2);\n");
    CHECK_FALSE(function.had_errors);
    CHECK(text_of(function.tokens) == "int x = ( ( 1 ) + ( 2 ) ) ;");
}

TEST_CASE("preprocessor supports stringize and token pasting") {
    const auto stringized = preprocess("#define STR(x) #x\nSTR(hello)\n");
    CHECK_FALSE(stringized.had_errors);
    REQUIRE(stringized.tokens.size() == 1);
    CHECK(stringized.tokens[0].text == "\"hello\"");

    const auto pasted = preprocess("#define CAT(a, b) a##b\nCAT(foo, bar)\n");
    CHECK_FALSE(pasted.had_errors);
    REQUIRE(pasted.tokens.size() == 1);
    CHECK(pasted.tokens[0].text == "foobar");
    CHECK(pasted.tokens[0].kind == krc::frontend::TokenKind::Identifier);
}

TEST_CASE("preprocessor evaluates conditionals") {
    const auto source = R"(
#if 1
int selected;
#else
int rejected;
#endif

#ifdef ENABLED
int enabled;
#endif

#ifndef ENABLED
int disabled;
#endif
)";
    const auto result = preprocess(source);
    CHECK_FALSE(result.had_errors);
    CHECK(text_of(result.tokens) == "int selected ; int disabled ;");
}

TEST_CASE("preprocessor evaluates defined and target macros") {
    const auto result = preprocess("#if defined(__kr32__) && __STDC_VERSION__ >= 201112L\nint ok;\n#endif\n");
    CHECK_FALSE(result.had_errors);
    CHECK(text_of(result.tokens) == "int ok ;");
}

TEST_CASE("preprocessor includes headers and honors pragma once") {
    const auto header = std::filesystem::current_path() / "krc_preprocessor_test_header.h";
    {
        std::ofstream file(header, std::ios::binary | std::ios::trunc);
        REQUIRE(file);
        file << "#pragma once\nint header_value;\n";
    }

    const auto result = preprocess("#include <krc_preprocessor_test_header.h>\n"
                                   "#include <krc_preprocessor_test_header.h>\n"
                                   "int main_value;\n",
                                   {std::filesystem::current_path()});
    std::error_code error;
    std::filesystem::remove(header, error);

    CHECK_FALSE(result.had_errors);
    CHECK(text_of(result.tokens) == "int header_value ; int main_value ;");
}

TEST_CASE("preprocessor honors include guards in headers") {
    const auto header = std::filesystem::current_path() / "krc_preprocessor_guard_header.h";
    {
        std::ofstream file(header, std::ios::binary | std::ios::trunc);
        REQUIRE(file);
        file << "#ifndef KRC_PREPROCESSOR_GUARD_HEADER\n"
                "#define KRC_PREPROCESSOR_GUARD_HEADER\n"
                "int guarded_value;\n"
                "#endif\n";
    }

    const auto result = preprocess("#include <krc_preprocessor_guard_header.h>\n"
                                   "#include <krc_preprocessor_guard_header.h>\n"
                                   "int main_value;\n",
                                   {std::filesystem::current_path()});
    std::error_code error;
    std::filesystem::remove(header, error);

    CHECK_FALSE(result.had_errors);
    CHECK(text_of(result.tokens) == "int guarded_value ; int main_value ;");
}

TEST_CASE("preprocessor applies #line to __LINE__ and __FILE__") {
    const auto result = preprocess("#line 100 \"virtual.c\"\n__FILE__ __LINE__;\n");
    CHECK_FALSE(result.had_errors);
    REQUIRE(result.tokens.size() == 3);
    CHECK(result.tokens[0].text == "\"virtual.c\"");
    CHECK(result.tokens[1].text == "100");
    CHECK(result.tokens[2].text == ";");
}

TEST_CASE("preprocessor provides deterministic date and time macros") {
    const auto result = preprocess("__DATE__ __TIME__\n");
    CHECK_FALSE(result.had_errors);
    REQUIRE(result.tokens.size() == 2);
    CHECK(result.tokens[0].text == "\"Jan  1 1970\"");
    CHECK(result.tokens[1].text == "\"00:00:00\"");
}

TEST_CASE("preprocessor evaluates __has_include") {
    const auto header = std::filesystem::current_path() / "krc_has_include_header.h";
    {
        std::ofstream file(header, std::ios::binary | std::ios::trunc);
        REQUIRE(file);
        file << "int has_include_value;\n";
    }

    const auto result = preprocess("#if __has_include(<krc_has_include_header.h>)\n"
                                   "int present;\n"
                                   "#endif\n"
                                   "#if !__has_include(<missing_has_include_header.h>)\n"
                                   "int absent;\n"
                                   "#endif\n",
                                   {std::filesystem::current_path()});
    std::error_code error;
    std::filesystem::remove(header, error);

    CHECK_FALSE(result.had_errors);
    CHECK(text_of(result.tokens) == "int present ; int absent ;");
}

TEST_CASE("preprocessor reports a missing header") {
    const auto result = preprocess("#include <missing_header_for_krc_test.h>\n");
    CHECK(result.had_errors);
}

TEST_CASE("preprocessor reports #error") {
    const auto result = preprocess("#error deliberate failure\n");
    CHECK(result.had_errors);
}
