#include <doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "krc/debug/kdbg.h"
#include "krc/toolchain.h"

// Producer-side tests for the kr216 debug bridge and the KDBG payload it becomes.
//
// The kr216 backend is the direct-bridge style: it appends S/L/F/K/V/W/G/T/C/M/A/R
// while it emits text, and the linker resolves every label after layout. These
// cases pin the contract a debugger consumes: truthful per-file line records, a
// definition-line record at each function entry, params/locals with kinds and no
// compiler temporaries, an F record and unique range for *every* lowered function
// (internal linkage included), G records for internal-linkage data, and the
// ADR 0052 promotion policy at -O2.

namespace {

#ifndef KRC_SOURCE_DIR
#define KRC_SOURCE_DIR "."
#endif

using krc::toolchain::Options;
using krc::toolchain::Source;
using krc::toolchain::Target;

Source source(std::string name, std::string_view text) {
    return Source{std::move(name), text};
}

Options kr216_options(bool cxx, int level) {
    Options options;
    options.target = Target::Kr216;
    options.cxx = cxx;
    options.optimize = level;
    return options;
}

krc::debug::KdbgImage decode(const std::vector<std::uint8_t>& container) {
    krc::debug::KdbgImage image;
    std::string error;
    REQUIRE_MESSAGE(krc::debug::decode_kdbg_image(container, image, error), error);
    return image;
}

// One line per '\n'; the fixtures below end in a newline, so this is the number
// of lines an editor shows.
std::uint32_t line_count(std::string_view text) {
    return static_cast<std::uint32_t>(
        std::count(text.begin(), text.end(), '\n'));
}

// The 1-based line of the unique line containing `token`; 0 when the token is
// absent or ambiguous, which a caller turns into a failure. Anchoring assertions
// on tokens rather than constants keeps them attached to the source text.
std::uint32_t line_of(std::string_view text, std::string_view token) {
    std::uint32_t line = 0;
    std::uint32_t found = 0;
    std::uint32_t matched = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        const std::size_t stop = newline == std::string_view::npos ? text.size() : newline;
        ++line;
        if (text.substr(start, stop - start).find(token) != std::string_view::npos) {
            ++found;
            matched = line;
        }
        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1;
    }
    return found == 1 ? matched : 0;
}

std::uint32_t source_lines(const std::string& recorded,
                           const std::map<std::string, std::uint32_t>& known) {
    const auto entry = known.find(recorded);
    if (entry != known.end()) {
        return entry->second;
    }
    std::filesystem::path path(recorded);
    if (!path.is_absolute()) {
        path = std::filesystem::path(KRC_SOURCE_DIR) / path;
    }
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        return 0;
    }
    std::ifstream stream(path);
    std::uint32_t lines = 0;
    std::string line;
    while (std::getline(stream, line)) {
        ++lines;
    }
    return lines;
}

// Every L record must name a line that exists in the file its source id names.
// This is the invariant that catches generated-assembly or previous-function
// line numbers leaking into a source file.
void check_lines_are_real(const krc::debug::KdbgImage& image,
                          const std::map<std::string, std::uint32_t>& known) {
    std::map<std::uint32_t, std::string> paths;
    for (const auto& file : image.sources) {
        paths.emplace(file.id, file.path);
    }
    for (const auto& line : image.lines) {
        const auto path = paths.find(line.source);
        REQUIRE_MESSAGE(path != paths.end(), "line names unknown source " << line.source);
        const std::uint32_t count = source_lines(path->second, known);
        REQUIRE_MESSAGE(count != 0, "unknown source file " << path->second);
        CHECK_MESSAGE((line.line >= 1 && line.line <= count),
                      path->second << " has " << count << " lines, record names "
                                   << line.line << " at 0x" << std::hex << line.address);
    }
}

const krc::debug::KdbgFunction* find_function(const krc::debug::KdbgImage& image,
                                              const std::string& name) {
    for (const auto& function : image.functions) {
        if (function.name == name) {
            return &function;
        }
    }
    return nullptr;
}

const krc::debug::KdbgVariable* find_variable(const krc::debug::KdbgImage& image,
                                              const std::string& function,
                                              const std::string& name) {
    for (const auto& variable : image.variables) {
        if (variable.function == function && variable.name == name) {
            return &variable;
        }
    }
    return nullptr;
}

const krc::debug::KdbgVarInfo* find_varinfo(const krc::debug::KdbgImage& image,
                                            const std::string& function,
                                            const std::string& name) {
    for (const auto& info : image.variable_info) {
        if (info.function == function && info.name == name) {
            return &info;
        }
    }
    return nullptr;
}

const krc::debug::KdbgType* find_type(const krc::debug::KdbgImage& image,
                                      const std::string& name) {
    for (const auto& type : image.types) {
        if (type.name == name) {
            return &type;
        }
    }
    return nullptr;
}

bool has_line(const krc::debug::KdbgImage& image, std::uint64_t address,
              std::uint32_t line) {
    for (const auto& record : image.lines) {
        if (record.address == address && record.line == line) {
            return true;
        }
    }
    return false;
}

std::vector<const krc::debug::KdbgLine*> lines_within(
    const krc::debug::KdbgImage& image, const krc::debug::KdbgFunction& function) {
    std::vector<const krc::debug::KdbgLine*> records;
    for (const auto& line : image.lines) {
        if (line.address >= function.start && line.address < function.end) {
            records.push_back(&line);
        }
    }
    return records;
}

// An F range must be non-empty and no two may overlap once sorted by start.
void check_function_ranges(const krc::debug::KdbgImage& image) {
    REQUIRE(!image.functions.empty());
    for (const auto& function : image.functions) {
        CHECK_MESSAGE(function.start < function.end, function.name);
    }
    std::vector<const krc::debug::KdbgFunction*> sorted;
    for (const auto& function : image.functions) {
        sorted.push_back(&function);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const krc::debug::KdbgFunction* left,
                 const krc::debug::KdbgFunction* right) {
                  return left->start < right->start;
              });
    for (std::size_t index = 1; index < sorted.size(); ++index) {
        CHECK_MESSAGE(sorted[index - 1]->end <= sorted[index]->start,
                      sorted[index - 1]->name << " and " << sorted[index]->name);
    }
    for (const auto& line : image.lines) {
        const bool inside =
            std::any_of(image.functions.begin(), image.functions.end(),
                        [&line](const krc::debug::KdbgFunction& function) {
                            return line.address >= function.start &&
                                   line.address < function.end;
                        });
        CHECK_MESSAGE(inside, "line at 0x" << std::hex << line.address);
    }
}

std::vector<std::string> bridge_records(std::string_view debug_info) {
    std::vector<std::string> records;
    std::size_t start = 0;
    while (start < debug_info.size()) {
        const std::size_t end = debug_info.find('\0', start);
        const std::size_t stop = end == std::string_view::npos ? debug_info.size() : end;
        records.emplace_back(debug_info.substr(start, stop - start));
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return records;
}

}  // namespace

TEST_CASE("kr216 C++ symbols name real lines, params and locals") {
    const std::string_view text =
        "struct Vec { int x; int y; };\n"
        "\n"
        "int g_exported = 5;\n"
        "static int file_only = 100;\n"
        "Vec g_vec = {3, 4};\n"
        "static int g_table[4] = {10, 20, 30, 40};\n"
        "\n"
        "static int hidden_add(int n) {\n"
        "    int adjusted = n + file_only;\n"
        "    return adjusted;\n"
        "}\n"
        "\n"
        "class Counter {\n"
        "public:\n"
        "    int stored;\n"
        "    int bump(int delta);\n"
        "};\n"
        "\n"
        "int Counter::bump(int delta) {\n"
        "    int before = stored;\n"
        "    int twice = delta + delta;\n"
        "    stored = before + twice + hidden_add(before);\n"
        "    return stored;\n"
        "}\n"
        "\n"
        "int sum_vec(Vec v) {\n"
        "    int total = v.x + v.y;\n"
        "    return total;\n"
        "}\n"
        "\n"
        "int main(void) {\n"
        "    Counter counter;\n"
        "    counter.stored = 7;\n"
        "    Vec local = {1, 2};\n"
        "    int pair[2];\n"
        "    pair[0] = sum_vec(local);\n"
        "    int bumped = counter.bump(3);\n"
        "    int shadow = 0;\n"
        "    {\n"
        "        int shadow = 4;\n"
        "        shadow = shadow + 1;\n"
        "        pair[1] = shadow;\n"
        "    }\n"
        "    return bumped + pair[0] + pair[1] + g_exported + g_vec.x + shadow + "
        "g_table[0];\n"
        "}\n";
    const auto options = kr216_options(true, 0);
    const auto built =
        krc::toolchain::build(std::vector<Source>{source("symbols.cpp", text)}, options);
    REQUIRE_MESSAGE(built.ok, built.error);
    const krc::debug::KdbgImage image = decode(built.image);

    // One source file, one line count: every record names a real line.
    check_lines_are_real(image, {{"symbols.cpp", line_count(text)}});
    bool saw_symbols_source = false;
    for (const auto& file : image.sources) {
        saw_symbols_source = saw_symbols_source || file.path == "symbols.cpp";
    }
    CHECK(saw_symbols_source);

    // Every function's entry address carries its definition line, in the file
    // the definition is written in (ADR 0061).
    const krc::debug::KdbgFunction* hidden = find_function(image, "hidden_add");
    REQUIRE(hidden != nullptr);
    CHECK(has_line(image, hidden->start,
                   line_of(text, "static int hidden_add(int n) {")));
    const krc::debug::KdbgFunction* bump = find_function(image, "Counter::bump");
    REQUIRE(bump != nullptr);
    CHECK(has_line(image, bump->start, line_of(text, "int Counter::bump(int delta) {")));
    const krc::debug::KdbgFunction* sum = find_function(image, "sum_vec");
    REQUIRE(sum != nullptr);
    CHECK(has_line(image, sum->start, line_of(text, "int sum_vec(Vec v) {")));
    const krc::debug::KdbgFunction* main = find_function(image, "main");
    REQUIRE(main != nullptr);
    CHECK(has_line(image, main->start, line_of(text, "int main(void) {")));

    // F ranges are well formed and every L record lands inside one.
    check_function_ranges(image);

    // The frame size and return type are real: a function with parameters and
    // locals has a frame, and int returns name i16 on this profile.
    CHECK(bump->frame > 0);
    CHECK_EQ(bump->type, std::string("i16"));
    CHECK_EQ(hidden->type, std::string("i16"));

    // Kinds: the parameter is a param and the declared locals are locals.
    const krc::debug::KdbgVarInfo* n = find_varinfo(image, "hidden_add", "n");
    REQUIRE(n != nullptr);
    CHECK(n->kind == krc::debug::KdbgVariableKind::Parameter);
    const krc::debug::KdbgVarInfo* this_info = find_varinfo(image, "Counter::bump", "this");
    REQUIRE(this_info != nullptr);
    CHECK(this_info->kind == krc::debug::KdbgVariableKind::Parameter);
    const krc::debug::KdbgVarInfo* delta =
        find_varinfo(image, "Counter::bump", "delta");
    REQUIRE(delta != nullptr);
    CHECK(delta->kind == krc::debug::KdbgVariableKind::Parameter);
    const krc::debug::KdbgVarInfo* before =
        find_varinfo(image, "Counter::bump", "before");
    REQUIRE(before != nullptr);
    CHECK(before->kind == krc::debug::KdbgVariableKind::Local);

    // A shadowed local's alloca is `shadow.addr.<n>`: it is recorded under its
    // unique display name (`shadow.<n>`) as a second local, not dropped and not
    // renamed onto the outer declaration.
    std::vector<const krc::debug::KdbgVarInfo*> shadows;
    for (const auto& info : image.variable_info) {
        if (info.function == "main" &&
            (info.name == "shadow" || info.name.rfind("shadow.", 0) == 0)) {
            shadows.push_back(&info);
        }
    }
    REQUIRE_EQ(shadows.size(), static_cast<std::size_t>(2));
    CHECK(shadows[0]->kind == krc::debug::KdbgVariableKind::Local);
    CHECK(shadows[1]->kind == krc::debug::KdbgVariableKind::Local);
    bool distinct_shadow = false;
    for (const auto* shadow : shadows) {
        if (shadow->name != "shadow") {
            distinct_shadow = true;
            CHECK(find_variable(image, "main", shadow->name) != nullptr);
        }
    }
    CHECK(distinct_shadow);

    // No compiler temporary ever becomes a variable row.
    for (const auto& variable : image.variables) {
        CHECK(variable.name.find("temporary") == std::string::npos);
        CHECK(variable.name.find(".temp") == std::string::npos);
        CHECK(variable.name.find("argument.copy") == std::string::npos);
        CHECK(variable.name.find("cond.tmp") == std::string::npos);
        CHECK(variable.name.find("subscript.this") == std::string::npos);
        CHECK(variable.name.find(".addr") == std::string::npos);
    }

    // Every V has a W with the same (function, name) and an accepted kind.
    for (const auto& variable : image.variables) {
        const krc::debug::KdbgVarInfo* info =
            find_varinfo(image, variable.function, variable.name);
        CHECK_MESSAGE(info != nullptr, variable.function << "." << variable.name);
        if (info != nullptr) {
            CHECK((info->kind == krc::debug::KdbgVariableKind::Parameter ||
                   info->kind == krc::debug::KdbgVariableKind::Local ||
                   info->kind == krc::debug::KdbgVariableKind::Static));
        }
    }

    // Aggregates and pointers keep real type sizes: Vec is two i16s, pair is an
    // array of two i16s, and `this` points at a Counter.
    const krc::debug::KdbgType* vec = find_type(image, "Vec");
    REQUIRE(vec != nullptr);
    CHECK_EQ(vec->size, 4u);
    CHECK_EQ(vec->kind, std::string("struct"));
    REQUIRE_EQ(vec->members.size(), static_cast<std::size_t>(2));
    CHECK_EQ(vec->members[0].name, std::string("x"));
    CHECK_EQ(vec->members[0].offset, 0u);
    CHECK_EQ(vec->members[1].name, std::string("y"));
    CHECK_EQ(vec->members[1].offset, 2u);
    const krc::debug::KdbgType* pair = find_type(image, "[2_x_i16]");
    REQUIRE(pair != nullptr);
    CHECK_EQ(pair->size, 4u);
    CHECK_EQ(pair->kind, std::string("array"));
    CHECK_EQ(pair->element_count, 2u);
    const krc::debug::KdbgType* counter = find_type(image, "Counter");
    REQUIRE(counter != nullptr);
    CHECK_EQ(counter->size, 2u);
    const krc::debug::KdbgType* counter_ptr = find_type(image, "ptr<Counter>");
    REQUIRE(counter_ptr != nullptr);
    CHECK_EQ(counter_ptr->size, 2u);
    CHECK_EQ(counter_ptr->kind, std::string("pointer"));
    CHECK_EQ(counter_ptr->target, std::string("Counter"));

    // External and internal-linkage globals are both present with real sizes.
    std::map<std::string, const krc::debug::KdbgSymbol*> globals;
    for (const auto& symbol : image.symbols) {
        globals.emplace(symbol.name, &symbol);
    }
    REQUIRE(globals.count("g_exported") == 1);
    CHECK_EQ(globals["g_exported"]->size, 2u);
    REQUIRE(globals.count("file_only") == 1);
    CHECK_EQ(globals["file_only"]->size, 2u);
    CHECK_EQ(globals["file_only"]->type, std::string("i16"));
    REQUIRE(globals.count("g_vec") == 1);
    CHECK_EQ(globals["g_vec"]->size, 4u);
    REQUIRE(globals.count("g_table") == 1);
    CHECK_EQ(globals["g_table"]->size, 8u);
}

TEST_CASE("kr216 F and G records are unique for internal-linkage symbols in two units") {
    const std::string_view unit_a =
        "static int counter = 3;\n"
        "\n"
        "static int helper(int value) {\n"
        "    int local = value + counter;\n"
        "    return local;\n"
        "}\n"
        "\n"
        "int alpha(int seed) {\n"
        "    return helper(seed);\n"
        "}\n";
    const std::string_view unit_b =
        "int alpha(int seed);\n"
        "\n"
        "static int counter = 4;\n"
        "\n"
        "static int helper(int value) {\n"
        "    int local = value + counter;\n"
        "    return local;\n"
        "}\n"
        "\n"
        "int beta(int seed) {\n"
        "    return helper(seed);\n"
        "}\n"
        "\n"
        "int main(void) {\n"
        "    return beta(alpha(1));\n"
        "}\n";
    auto options = kr216_options(false, 0);
    std::vector<Source> units;
    units.push_back(source("unit_a.c", unit_a));
    units.push_back(source("unit_b.c", unit_b));
    const auto built = krc::toolchain::build(units, options);
    REQUIRE_MESSAGE(built.ok, built.error);
    const krc::debug::KdbgImage image = decode(built.image);

    // Five bodies were lowered: two `helper`s, alpha, beta and main. Each has its
    // own F record and a well-formed range; the two same-named helpers are not
    // one range reported twice.
    REQUIRE_EQ(image.functions.size(), static_cast<std::size_t>(5));
    check_function_ranges(image);
    std::vector<const krc::debug::KdbgFunction*> helpers;
    for (const auto& function : image.functions) {
        if (function.name == "helper") {
            helpers.push_back(&function);
        }
    }
    REQUIRE_EQ(helpers.size(), static_cast<std::size_t>(2));
    CHECK(helpers[0]->start != helpers[1]->start);
    CHECK(helpers[0]->end != helpers[1]->end);

    // Each helper's line records stay in its own body: a line record inside a
    // helper range names the source that defines that helper, never the other
    // unit's file.
    std::map<std::uint32_t, std::string> source_paths;
    for (const auto& file : image.sources) {
        source_paths.emplace(file.id, file.path);
    }
    std::set<std::uint32_t> helper_sources;
    for (const auto* helper : helpers) {
        std::set<std::uint32_t> sources;
        for (const auto* line : lines_within(image, *helper)) {
            sources.insert(line->source);
        }
        REQUIRE_EQ(sources.size(), static_cast<std::size_t>(1));
        helper_sources.insert(*sources.begin());
    }
    CHECK_EQ(helper_sources.size(), static_cast<std::size_t>(2));
    for (const std::uint32_t id : helper_sources) {
        const auto path = source_paths.find(id);
        REQUIRE(path != source_paths.end());
        CHECK(path->second.find("unit_") != std::string::npos);
    }

    // The two file-scope statics named `counter` keep the source name and two
    // distinct addresses, so a debugger can list both.
    std::vector<const krc::debug::KdbgSymbol*> counters;
    for (const auto& symbol : image.symbols) {
        if (symbol.name == "counter") {
            counters.push_back(&symbol);
        }
    }
    REQUIRE_EQ(counters.size(), static_cast<std::size_t>(2));
    CHECK(counters[0]->address != counters[1]->address);
    for (const auto* counter : counters) {
        CHECK_EQ(counter->size, 2u);
        CHECK_EQ(counter->type, std::string("i16"));
    }

    // A duplicated body's V/W rows are recorded once, not once per copy.
    std::map<std::pair<std::string, std::string>, int> rows;
    for (const auto& variable : image.variables) {
        ++rows[{variable.function, variable.name}];
    }
    for (const auto& [key, count] : rows) {
        CHECK_EQ(count, 1);
    }

    // Every source's line records name real lines.
    check_lines_are_real(image, {{"unit_a.c", line_count(unit_a)},
                                 {"unit_b.c", line_count(unit_b)}});
}

TEST_CASE("kr216 F shows a member function's display name while V/W keep it") {
    const std::string_view text =
        "struct Widget {\n"
        "    int value;\n"
        "    int get(int k) { int tmp = value + k; return tmp; }\n"
        "};\n"
        "\n"
        "int use(void) {\n"
        "    Widget w;\n"
        "    w.value = 5;\n"
        "    return w.get(3);\n"
        "}\n"
        "\n"
        "int main(void) {\n"
        "    return use();\n"
        "}\n";
    const auto options = kr216_options(true, 0);
    const auto built =
        krc::toolchain::build(std::vector<Source>{source("display.cpp", text)}, options);
    REQUIRE_MESSAGE(built.ok, built.error);
    const krc::debug::KdbgImage image = decode(built.image);

    // The debugger shows `Widget::get`; the symbol is `Widget_get` and is not an
    // F name. This is the kr216 shape: V/W name the display name, so krdbg's
    // function identity falls back to the F name and member-function locals stay
    // visible without a K bridge.
    const krc::debug::KdbgFunction* get = find_function(image, "Widget::get");
    REQUIRE(get != nullptr);
    CHECK(find_function(image, "Widget_get") == nullptr);
    const krc::debug::KdbgVarInfo* this_info = find_varinfo(image, "Widget::get", "this");
    REQUIRE(this_info != nullptr);
    CHECK(this_info->kind == krc::debug::KdbgVariableKind::Parameter);
    CHECK(find_varinfo(image, "Widget_get", "this") == nullptr);
    CHECK(find_varinfo(image, "Widget::get", "k") != nullptr);

    // The function-scope K record keeps the symbol, and it starts at the same
    // address as F. krdbg only adopts it as the identity when the V/W rows name
    // it, which they do not here; the record is what a future consumer would use.
    const krc::debug::KdbgScope* scope = nullptr;
    for (const auto& record : image.scopes) {
        if (record.function == "Widget_get" && record.start == get->start) {
            scope = &record;
        }
    }
    REQUIRE(scope != nullptr);
    CHECK_EQ(scope->name, std::string("Widget_get"));
    CHECK(scope->end >= get->end);
}

TEST_CASE("kr216 keeps named locals at -O2 (ADR 0052 policy)") {
    const std::string_view text =
        "int compute(int seed) {\n"
        "    int doubled = seed + seed;\n"
        "    doubled = doubled + 1;\n"
        "    return doubled;\n"
        "}\n"
        "\n"
        "int main(void) {\n"
        "    int base = 3;\n"
        "    int result = compute(base);\n"
        "    result = result + base;\n"
        "    return result;\n"
        "}\n";
    for (const int level : {0, 1, 2}) {
        const auto options = kr216_options(false, level);
        const auto built =
            krc::toolchain::build(std::vector<Source>{source("promoted.c", text)},
                                  options);
        REQUIRE_MESSAGE(built.ok, built.error);
        const krc::debug::KdbgImage image = decode(built.image);

        // The keyed variables of the level: `doubled` is promoted away by a
        // level-only gate, `base`/`result` belong to main which cannot be inlined
        // away. Every one of them is still a frame-resident local at every level.
        const krc::debug::KdbgVarInfo* doubled = find_varinfo(image, "compute", "doubled");
        if (level < 2) {
            REQUIRE_MESSAGE(doubled != nullptr, "-O" << level);
            CHECK(doubled->kind == krc::debug::KdbgVariableKind::Local);
        } else if (doubled != nullptr) {
            CHECK(doubled->kind == krc::debug::KdbgVariableKind::Local);
        }
        const krc::debug::KdbgVarInfo* base = find_varinfo(image, "main", "base");
        REQUIRE_MESSAGE(base != nullptr, "-O" << level);
        CHECK(base->kind == krc::debug::KdbgVariableKind::Local);
        const krc::debug::KdbgVarInfo* result = find_varinfo(image, "main", "result");
        REQUIRE_MESSAGE(result != nullptr, "-O" << level);
        CHECK(result->kind == krc::debug::KdbgVariableKind::Local);
        CHECK(find_variable(image, "main", "base") != nullptr);
        CHECK(find_variable(image, "main", "result") != nullptr);

        // No target has a P/Location producer, so the frames carry no register
        // ranges: -O2 is unsupported for inspection until one exists rather than
        // being described by a record that is not true.
        CHECK(image.locations.empty());
    }
}

TEST_CASE("kr216 bridge line labels all exist in the emitted assembly") {
    const std::string_view text =
        "int add(int a, int b) {\n"
        "    int sum = a + b;\n"
        "    return sum;\n"
        "}\n"
        "\n"
        "int twice(int value) {\n"
        "    int doubled = value + value;\n"
        "    return doubled;\n"
        "}\n"
        "\n"
        "int main(void) {\n"
        "    return add(1, 2) + twice(3);\n"
        "}\n";
    const auto options = kr216_options(false, 1);
    const auto compiled = krc::toolchain::compile(source("bridge.c", text), options);
    REQUIRE_MESSAGE(compiled.ok, compiled.error);

    // Every label a line record names survived the -O1 text passes: the linker
    // resolves those names, so a label the passes deleted would silently drop the
    // line. The per-object prefix also keeps each function's labels distinct.
    std::set<std::string> labels;
    std::size_t line_records = 0;
    for (const std::string& record : bridge_records(compiled.debug_info)) {
        if (record.rfind("L ", 0) != 0) {
            continue;
        }
        std::istringstream stream(record);
        std::string tag;
        std::string label;
        std::uint32_t source_id = 0;
        std::uint32_t line = 0;
        std::uint32_t column = 0;
        stream >> tag >> label >> source_id >> line >> column;
        CHECK_EQ(source_id, 0u);
        CHECK(line >= 1);
        CHECK(line <= line_count(text));
        CHECK_MESSAGE(labels.insert(label).second, "duplicate label " << label);
        CHECK_MESSAGE(compiled.assembly.find(label + ":") != std::string::npos,
                      "line label missing from the assembly: " << label);
        ++line_records;
    }
    CHECK(line_records > 0);
    // The definition-line label is a begin label; the instruction labels keep the
    // `.Ldbg.` prefix the text passes preserve.
    CHECK(compiled.assembly.find(".Ldbg.") != std::string::npos);
    CHECK(compiled.assembly.find(".begin:") != std::string::npos);
    // The F end label sits before the epilogue, so a pc in the epilogue belongs to
    // no function (KR32/KR16's documented convention).
    CHECK(compiled.assembly.find(".ret:\n    LEAVE #0\n    RET\n") !=
          std::string::npos);
}

TEST_CASE("kr216 assembly input carries no fabricated debug records") {
    // The direct-bridge style keeps the debug text in-process: a `.s` input (hand
    // written, or the output of `-S`) carries no bridge, so its image honestly has
    // no `.kdbg` payload instead of line records with generated-assembly numbers
    // under a source name.
    const std::string assembly =
        ".text\n"
        ".global main\n"
        "main:\n"
        "    ld r2, #0\n"
        "    sys\n";
    Options options;
    options.target = Target::Kr216;
    options.entry = "main";
    options.source_name = "hand.s";
    const auto assembled = krc::toolchain::assemble(assembly, options);
    REQUIRE_MESSAGE(assembled.ok, assembled.error);
    std::vector<krc::toolchain::Object> objects;
    objects.push_back(krc::toolchain::Object{"hand.o", assembled.object});
    const auto linked = krc::toolchain::link(objects, options);
    REQUIRE_MESSAGE(linked.ok, linked.error);

    krc::debug::KdbgImage image;
    std::string error;
    CHECK_FALSE(krc::debug::decode_kdbg_image(linked.image, image, error));
    CHECK(error.find(".kdbg") != std::string::npos);
    CHECK(image.lines.empty());
    CHECK(image.functions.empty());
}

TEST_CASE("kr216 include-defined code names its own source file") {
    // A member function defined in a header is emitted into this translation unit
    // but its line records must name the header, not the including .cpp. The main
    // source is compiled from memory; the header is read from the fixture.
    const std::string_view text =
        "#include \"debug_include.hpp\"\n"
        "\n"
        "int main(void) {\n"
        "    Span span;\n"
        "    span.first = 1;\n"
        "    span.second = 2;\n"
        "    return span.sum();\n"
        "}\n";
    const std::string include_dir =
        (std::filesystem::path(KRC_SOURCE_DIR) / "tests" / "fixtures" / "kr216")
            .string();
    auto options = kr216_options(true, 0);
    options.include_paths.push_back(include_dir);
    const auto built = krc::toolchain::build(
        std::vector<Source>{source("debug_include.cpp", text)}, options);
    REQUIRE_MESSAGE(built.ok, built.error);
    const krc::debug::KdbgImage image = decode(built.image);

    // Both files are named: the .cpp that defines main and the header that
    // defines Span::sum.
    std::map<std::uint32_t, std::string> paths;
    for (const auto& file : image.sources) {
        paths.emplace(file.id, file.path);
    }
    std::uint32_t header_id = UINT32_MAX;
    std::uint32_t cpp_id = UINT32_MAX;
    for (const auto& [id, path] : paths) {
        const std::string name = std::filesystem::path(path).filename().string();
        if (name == "debug_include.hpp") {
            header_id = id;
        } else if (name == "debug_include.cpp") {
            cpp_id = id;
        }
    }
    REQUIRE(header_id != UINT32_MAX);
    REQUIRE(cpp_id != UINT32_MAX);

    const krc::debug::KdbgFunction* span_sum = find_function(image, "Span::sum");
    REQUIRE(span_sum != nullptr);
    const krc::debug::KdbgFunction* main = find_function(image, "main");
    REQUIRE(main != nullptr);

    const std::string header_text = [] {
        const std::filesystem::path path =
            std::filesystem::path(KRC_SOURCE_DIR) / "tests" / "fixtures" / "kr216" /
            "debug_include.hpp";
        std::ifstream stream(path);
        std::string text((std::istreambuf_iterator<char>(stream)),
                         std::istreambuf_iterator<char>());
        return text;
    }();
    REQUIRE(header_text.find("int sum()") != std::string::npos);
    const std::uint32_t header_definition = line_of(header_text, "int sum()");
    REQUIRE(header_definition != 0);

    // The entry address of Span::sum carries a line record for the header's
    // definition line; main's carries one for the .cpp's.
    bool header_entry = false;
    bool cpp_entry = false;
    for (const auto& line : image.lines) {
        if (line.address == span_sum->start && line.source == header_id &&
            line.line == header_definition) {
            header_entry = true;
        }
        if (line.address == main->start && line.source == cpp_id &&
            line.line == line_of(text, "int main(void) {")) {
            cpp_entry = true;
        }
    }
    CHECK(header_entry);
    CHECK(cpp_entry);

    // Every record names a real line of its own file.
    check_lines_are_real(image, {{"debug_include.cpp", line_count(text)}});
}
