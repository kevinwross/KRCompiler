#include <stdexcept>
#include <string>

#include "doctest.h"
#include "krc/frontend/source_manager.h"

using krc::frontend::LineColumn;
using krc::frontend::SourceLocation;
using krc::frontend::SourceManager;

TEST_CASE("source manager maps offsets to lines and columns") {
    SourceManager sources;
    const auto file = sources.add_buffer("test.c", "one\ntwo\nthree");

    CHECK(sources.file_count() == 1);
    CHECK(sources.file_name(file) == "test.c");

    const auto first = sources.line_column(sources.location(file, 0));
    CHECK(first.line == 1);
    CHECK(first.column == 1);

    const auto second = sources.line_column(sources.location(file, 4));
    CHECK(second.line == 2);
    CHECK(second.column == 1);

    const auto third = sources.line_column(sources.location(file, 8));
    CHECK(third.line == 3);
    CHECK(third.column == 1);

    CHECK(std::string(sources.line_text(sources.location(file, 4))) == "two");
    CHECK(std::string(sources.spelling(sources.location(file, 4), 3)) == "two");
}

TEST_CASE("source manager applies #line remapping") {
    SourceManager sources;
    const auto file = sources.add_buffer("test.c", "line one\nline two\nline three\n");
    sources.add_line_directive(file, 9, 2, 100);

    CHECK(sources.line_column(sources.location(file, 0)).line == 1);
    CHECK(sources.line_column(sources.location(file, 9)).line == 100);
    CHECK(sources.line_column(sources.location(file, 18)).line == 101);
}

TEST_CASE("source manager handles empty input and end of file") {
    SourceManager sources;
    const auto file = sources.add_buffer("empty.c", "");

    const auto location = sources.location(file, 0);
    CHECK(location.valid());
    CHECK(sources.line_column(location).line == 1);
    CHECK(sources.line_column(location).column == 1);
    CHECK(sources.line_text(location).empty());

    const auto beyond = sources.location(file, 100);
    CHECK(beyond.offset == 0);
}

TEST_CASE("source manager rejects invalid file ids") {
    SourceManager sources;
    const auto access = [&sources]() -> const auto& { return sources.file(0); };
    CHECK_THROWS_AS(access(), std::out_of_range);

    const auto invalid = sources.location(99, 0);
    CHECK_FALSE(invalid.valid());
    CHECK(sources.file_name(99).empty());
}

TEST_CASE("source manager tracks the include stack") {
    SourceManager sources;
    const auto main_file = sources.add_buffer("main.c", "int main(void) { return 0; }\n");
    const auto header = sources.add_buffer("header.h", "int value;\n");

    CHECK(sources.current_file() == SourceLocation::kInvalidFile);
    sources.push_include(main_file);
    sources.push_include(header);
    CHECK(sources.include_stack().size() == 2);
    CHECK(sources.current_file() == header);
    sources.pop_include();
    CHECK(sources.current_file() == main_file);
    sources.pop_include();
    CHECK(sources.current_file() == SourceLocation::kInvalidFile);
}

TEST_CASE("source manager reports a missing file") {
    SourceManager sources;
    std::string error;
    const auto file = sources.load_file("this-file-does-not-exist-krc-test.c", error);
    CHECK_FALSE(file.has_value());
    CHECK_FALSE(error.empty());
}
