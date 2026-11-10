#include <sstream>
#include <string>

#include "doctest.h"
#include "krc/frontend/diagnostics.h"

using krc::frontend::DiagnosticEngine;
using krc::frontend::Severity;
using krc::frontend::SourceLocation;
using krc::frontend::SourceManager;

TEST_CASE("diagnostics print locations, source lines, and notes") {
    SourceManager sources;
    const auto file = sources.add_buffer("test.c", "int main() {\n  return x;\n}\n");
    const auto text = sources.file(file).text();
    const auto offset = text.find("x;");
    REQUIRE(offset != std::string::npos);

    DiagnosticEngine diagnostics(sources);
    diagnostics.report(Severity::Error, sources.location(file, static_cast<std::uint32_t>(offset)),
                       "use of undeclared identifier 'x'");
    diagnostics.note(sources.location(file, static_cast<std::uint32_t>(offset)), "not declared");

    std::ostringstream out;
    diagnostics.print(out);
    const auto printed = out.str();

    CHECK(printed.find("test.c:2:10: error: use of undeclared identifier 'x'") !=
          std::string::npos);
    CHECK(printed.find("return x;") != std::string::npos);
    CHECK(printed.find("^") != std::string::npos);
    CHECK(printed.find("note: not declared") != std::string::npos);
    CHECK(diagnostics.has_errors());
    CHECK(diagnostics.error_count() == 1);
}

TEST_CASE("diagnostics count warnings separately") {
    SourceManager sources;
    const auto file = sources.add_buffer("test.c", "int x;\n");
    DiagnosticEngine diagnostics(sources);
    diagnostics.report(Severity::Warning, sources.location(file, 0), "unused variable");

    CHECK_FALSE(diagnostics.has_errors());
    CHECK(diagnostics.warning_count() == 1);
    CHECK(diagnostics.error_count() == 0);
}

TEST_CASE("diagnostics stop at the error limit") {
    SourceManager sources;
    const auto file = sources.add_buffer("test.c", "int x;\n");
    DiagnosticEngine diagnostics(sources);
    diagnostics.set_error_limit(2);

    for (int i = 0; i < 5; ++i) {
        diagnostics.report(Severity::Error, sources.location(file, 0), "error");
    }

    REQUIRE(diagnostics.diagnostics().size() == 3);
    CHECK(diagnostics.diagnostics().back().message.find("too many errors") != std::string::npos);
    CHECK(diagnostics.has_errors());
}

TEST_CASE("diagnostics sort by source order") {
    SourceManager sources;
    const auto file = sources.add_buffer("test.c", "int x;\nint y;\n");
    DiagnosticEngine diagnostics(sources);
    diagnostics.report(Severity::Error, sources.location(file, 12), "second");
    diagnostics.report(Severity::Warning, sources.location(file, 0), "first");
    diagnostics.sort();

    REQUIRE(diagnostics.diagnostics().size() == 2);
    CHECK(diagnostics.diagnostics()[0].location.offset == 0);
    CHECK(diagnostics.diagnostics()[1].location.offset == 12);
}

TEST_CASE("diagnostics can be cleared") {
    SourceManager sources;
    DiagnosticEngine diagnostics(sources);
    diagnostics.report(Severity::Error, "no location");
    CHECK(diagnostics.has_errors());
    diagnostics.clear();
    CHECK_FALSE(diagnostics.has_errors());
    CHECK(diagnostics.diagnostics().empty());
}
