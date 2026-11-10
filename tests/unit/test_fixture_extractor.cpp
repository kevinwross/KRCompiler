#include <string>

#include "doctest.h"
#include "krc/tools/fixture_extractor.h"

using krc::tools::extract_fixtures;

TEST_CASE("fixture extractor concatenates adjacent compile arguments") {
    const std::string source = R"SRC(
const char* ignored = "not a fixture";
TEST_CASE("extracts") {
    cc::compile("int main(void) {\n"
                "    return 0;\n"
                "}\n");
}
)SRC";

    const auto fixtures = extract_fixtures(source, "test_cc_sample.cpp");
    REQUIRE(fixtures.size() == 1);
    CHECK(fixtures[0].line == 4);
    CHECK(fixtures[0].text == "int main(void) {\n    return 0;\n}\n");
    CHECK(fixtures[0].source_name == "test_cc_sample.cpp");
}

TEST_CASE("fixture extractor handles compile_and_run and raw strings") {
    const std::string source = R"SRC(
compile_and_run("int x = 1;\n" "int main(void) { return x; }\n");
cc::compile(R"(int main(void) { return 0; })");
)SRC";

    const auto fixtures = extract_fixtures(source, "sample.cpp");
    REQUIRE(fixtures.size() == 2);
    CHECK(fixtures[0].text == "int x = 1;\nint main(void) { return x; }\n");
    CHECK(fixtures[1].text == "int main(void) { return 0; }");
}

TEST_CASE("fixture extractor decodes escapes") {
    const std::string source = R"SRC(
compile("\x41\101\u0042\n");
)SRC";

    const auto fixtures = extract_fixtures(source, "sample.c");
    REQUIRE(fixtures.size() == 1);
    CHECK(fixtures[0].text == "AAB\n");
}

TEST_CASE("fixture extractor ignores strings and comments") {
    const std::string source = R"SRC(
const char* text = "compile(\"not a call\")";
// compile("also not a call");
/* compile("still not a call"); */
)SRC";

    CHECK(extract_fixtures(source, "sample.c").empty());
}
